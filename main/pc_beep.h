// main/pc_beep.h
// PC Controller 平台层:按键音(短促提示音,FR-09)。
//
// 职责:在有效按键动作分发点播放短促合成音,给用户可听反馈
// (ui-design §7:key sound / rejected presses)。三种音色:
//   确认(高频正弦)/ 翻页(中频正弦)/ 错误(低频方波)。
//
// 配置开关:全局键音开关来自配置项 `pp_cfg.key_sound`(规格 §8),
// 默认【关】(规格 §1/FR-09:globally mutable, default off)。
// 组装层在载入配置后经 pc_beep_set_enabled() 同步开关;开关关闭
// 时本模块不初始化音频、不产生任何 I2C/I2S 流量。
//
// 失败降级(规格 §10:"Audio init/play failure -> key sound and all
// beeps degrade to silence; the feature flag stays on"):
//   - bsp_audio 延迟到首次真正播放时才初始化(启动期不占用
//     I2C/I2S 资源,启动失败也不影响其余外设);
//   - 任何初始化/播放失败仅记日志,功能路径(按键语义 -> HID)
//     完全不受影响;开关位保持用户设定,不回退。
//
// 采样参数:16 kHz / 16 bit / 单声道 —— 与 bsp_audio 常用播放
// 格式对齐(仓库音频链路无采样率切换需求,避免触发
// bsp_audio_set_format 的 close/reopen 路径,见 bsp_audio.h 坑注)。
//
// 线程上下文:全部接口由应用任务调用(按键动作分发点);
// pc_beep_play 内部的 bsp_audio_write 为阻塞 DMA 写,时长 <=
// 单次音长(约 150 ms),在按键节奏下可接受(规格 §7)。
// 内存所有权:静态缓冲区,不分配堆内存。
#pragma once

#include <stdbool.h>

/* 按键音种类。 */
typedef enum {
    PC_BEEP_CONFIRM = 0, /* 确认:高频短促正弦(菜单确认/模式进入等) */
    PC_BEEP_PAGE,        /* 翻页:中频短促正弦(演示翻页/音量步进) */
    PC_BEEP_ERROR,       /* 错误:低频方波(无效/被拒绝的按键,
                          * ui-design §7:distinct low tone) */
} pc_beep_kind_t;

/* 模块状态初始化(轻量)。
 * 行为:仅复位内部状态(开关默认关、音频未初始化);【不】触碰
 *      bsp_audio——音频硬件延迟到首次播放时初始化(见文件头)。
 * 返回值:无。失败值:无。
 * 线程上下文:启动阶段,应用任务。 */
void pc_beep_init(void);

/* 同步全局键音开关(配置项 `pp_cfg.key_sound`,FR-09)。
 * 参数:enabled true = 开;默认关。开关关闭时播放调用直接返回。
 * 线程上下文:应用任务;非阻塞。 */
void pc_beep_set_enabled(bool enabled);

/* 播放一次按键音。
 * 行为:开关关 / 音频持久失败 -> 直接返回(静默);首次播放时
 *      初始化音频(失败 -> 标记持久失败,静默降级,规格 §10);
 *      内联合成该音色的 16 kHz/16 bit mono PCM 并经
 *      bsp_audio_write 播放(阻塞至播放完成,<= 150 ms)。
 * 参数:kind 音色;越界值按"不播放"处理(防御式)。
 * 返回值:无。失败值:无(一切音频失败静默降级,仅日志)。
 * 线程上下文:应用任务。 */
void pc_beep_play(pc_beep_kind_t kind);
