// main/pc_beep.c
// 按键音实现(接口说明见头文件)。
//
// 合成方式:内联数字合成——正弦(确认/翻页)与方波(错误)直接
// 生成 16 kHz / 16 bit / mono PCM 写入静态缓冲,经
// bsp_audio_write 一次性送出。不引入音频资源文件(仓库
// assets/music 无本应用素材,且 Flash/RAM 预算从紧,规格 §9)。
#include "pc_beep.h"

#include "bsp_audio.h"

#include "esp_log.h"

#include <math.h>
#include <stdint.h>

static const char *TAG = "pc_beep";

/* ---- 采样参数(与 bsp_audio 常用格式对齐,见头文件) ---- */
#define PC_BEEP_SAMPLE_HZ 16000U /* 采样率 */
#define PC_BEEP_BITS      16U    /* 位深 */
#define PC_BEEP_CHANNELS  1U     /* 单声道 */
#define PC_BEEP_VOLUME    60U    /* codec 输出音量 0..100(提示音
                                  * 取中偏上,不抢演示场景) */

/* 最长音 150 ms -> 2400 采样;静态缓冲 4.8 KB(规格 §9:计入
 * 内部 RAM 评估,presenter 档无 Wi-Fi 释放的 30-50 KB 可覆盖)。 */
#define PC_BEEP_MAX_SAMPLES 2400U
/* 起止各 3 ms 线性淡入淡出,避免方波/截断爆音。 */
#define PC_BEEP_FADE_SAMPLES 48U

static int16_t s_pcm[PC_BEEP_MAX_SAMPLES]; /* 合成缓冲(应用任务独占) */

/* 音色定义:频率 / 时长 / 波形 / 振幅(振幅取 16 bit 满幅的
 * 约 1/4-1/3,短促提示音无需大动态)。 */
typedef struct {
    uint32_t freq_hz; /* 基频 */
    uint32_t ms;      /* 音长(<= 150) */
    bool square;      /* true = 方波(错误音);false = 正弦 */
    int16_t amp;      /* 峰值振幅 */
} beep_def_t;

static const beep_def_t s_defs[3] = {
    [PC_BEEP_CONFIRM] = { 1760U, 70U,  false, 9000 }, /* A6,确认 */
    [PC_BEEP_PAGE]    = { 1319U, 50U,  false, 8000 }, /* E6,翻页 */
    [PC_BEEP_ERROR]   = {  247U, 140U, true,  7000 }, /* B3,错误低音
                                                       * (ui-design §7) */
};

/* ---- 文件内状态(应用任务独占) ---- */
static bool s_enabled;     /* 键音开关(pp_cfg.key_sound,默认关) */
static bool s_audio_ready; /* bsp_audio 已初始化成功 */
static bool s_audio_dead;  /* 音频持久失败 -> 永久静默(规格 §10) */

/* 延迟初始化音频(首次播放才调用)。
 * 返回:true 可播放;false 已标记持久失败(仅日志,静默降级)。
 * 依据:规格 §10"Audio init/play failure -> degrade to silence;
 * the feature flag stays on"——开关位不受影响。 */
static bool ensure_audio(void)
{
    if (s_audio_ready) return true;
    if (s_audio_dead) return false;

    /* bsp_audio_init 内部幂等初始化 I2C(bsp_audio.h),失败常见
     * 原因是 I2C 总线初始化失败(组装层已有同款降级日志)。 */
    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) {
        err = bsp_audio_set_format(PC_BEEP_SAMPLE_HZ, PC_BEEP_BITS,
                                    PC_BEEP_CHANNELS);
    }
    if (err == ESP_OK) {
        bsp_audio_set_volume(PC_BEEP_VOLUME);
        s_audio_ready = true;
        return true;
    }
    s_audio_dead = true; /* 后续播放恒静默,不再重试冲击总线 */
    ESP_LOGW(TAG, "audio init failed: %s; key sound degrades to "
                  "silence (spec S10), feature flag unchanged",
             esp_err_to_name(err));
    return false;
}

/* 按音色定义合成 PCM 到静态缓冲,返回采样数。
 * 波形:正弦用 libm sinf(ESP32-C3 无 FPU,单音 2400 采样的软浮点
 * 开销约毫秒级,按键节奏下可接受);方波按正弦相位符号取幅值。
 * 包络:头尾各 PC_BEEP_FADE_SAMPLES 个采样线性渐变,防爆音。 */
static uint32_t synth(const beep_def_t *d)
{
    uint32_t n = (PC_BEEP_SAMPLE_HZ / 1000U) * d->ms;
    if (n > PC_BEEP_MAX_SAMPLES) n = PC_BEEP_MAX_SAMPLES;

    const float phase_step =
        2.0f * (float)M_PI * (float)d->freq_hz / (float)PC_BEEP_SAMPLE_HZ;
    float phase = 0.0f;
    for (uint32_t i = 0U; i < n; i++) {
        /* 线性包络:淡入段 0->1,淡出段 1->0,中段恒 1 */
        float env = 1.0f;
        if (i < PC_BEEP_FADE_SAMPLES) {
            env = (float)i / (float)PC_BEEP_FADE_SAMPLES;
        } else if (i + PC_BEEP_FADE_SAMPLES >= n) {
            env = (float)(n - i) / (float)PC_BEEP_FADE_SAMPLES;
        }
        float s = sinf(phase);
        if (d->square) {
            s = (s >= 0.0f) ? 1.0f : -1.0f; /* 方波:只取相位符号 */
        }
        float v = s * env * (float)d->amp;
        /* 限幅:包络/振幅组合理论上不越界,防御式钳制 */
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        s_pcm[i] = (int16_t)v;
        phase += phase_step;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    return n;
}

void pc_beep_init(void)
{
    /* 轻量状态初始化:音频硬件延迟到首次播放(见头文件与
     * ensure_audio;规格 §10 降级引用同上)。 */
    s_enabled = false;
    s_audio_ready = false;
    s_audio_dead = false;
}

void pc_beep_set_enabled(bool enabled)
{
    s_enabled = enabled; /* FR-09:全局可变,默认关 */
}

void pc_beep_play(pc_beep_kind_t kind)
{
    if (!s_enabled || s_audio_dead) return;
    if ((unsigned)kind >= (unsigned)(sizeof(s_defs) / sizeof(s_defs[0]))) {
        return; /* 越界音色:防御式忽略 */
    }
    if (!ensure_audio()) return;

    uint32_t n = synth(&s_defs[kind]);
    /* bsp_audio_write:字节数 = 采样数 x 2(16 bit);阻塞 DMA 写,
     * 播放完成才返回(<= 150 ms)。 */
    esp_err_t err = bsp_audio_write(s_pcm, (size_t)n * 2U);
    if (err != ESP_OK) {
        /* 播放失败:静默降级,仅日志;不标记 dead(初始化已成功,
         * 单次投递失败可能为瞬态),不阻塞按键功能路径。 */
        ESP_LOGW(TAG, "beep play failed: %s (silence, key path unaffected)",
                 esp_err_to_name(err));
    }
}
