// main/pc_speech_timer.c
// 演讲计时器实现(接口说明见头文件)。
//
// 平台无关:仅依赖 C11 标准库,可被 host 测试直接编译。
#include "pc_speech_timer.h"

#include <stddef.h>
#include <stdio.h>

/* 显示上限:99:59:59 = 359999 秒(任务 #47 自适应格式后)。
 * 依据:屏显缓冲固定 9 字节,"HH:MM:SS" + '\0' 恰好 8 + 1 = 9 字节;
 * 演讲超过 100 小时无显示意义,直接钳制。 */
#define PC_SPEECH_DISPLAY_MAX_SEC 359999U

/* 1 小时秒数,任务 #47 HH:MM:SS 格式阈值。 */
#define PC_SPEECH_HOUR_SEC 3600U

void pc_speech_reset(pc_speech_timer_t *t)
{
    if (t == NULL) {
        return;
    }
    t->sec = 0U;
    /* 任务 #47:进入演示模式 (TIMER_RESET) 一并解除暂停,
     * 避免上次残留 paused=true 状态导致新一轮演示不计数。 */
    t->paused = false;
}

void pc_speech_tick(pc_speech_timer_t *t)
{
    if (t == NULL) {
        return;
    }
    /* 任务 #47:暂停期间 tick 为无效操作, 保持 sec 不变;
     * 这样 1 Hz 定时源不需要感知暂停状态, 由本模块内部仲裁。 */
    if (t->paused) {
        return;
    }
    /* 自然自增;32 位秒数理论 136 年回绕,不做饱和。 */
    t->sec++;
}

uint32_t pc_speech_seconds(const pc_speech_timer_t *t)
{
    return t != NULL ? t->sec : 0U;
}

bool pc_speech_is_paused(const pc_speech_timer_t *t)
{
    return (t != NULL) && t->paused;
}

void pc_speech_set_paused(pc_speech_timer_t *t, bool paused)
{
    if (t == NULL) {
        return;
    }
    t->paused = paused;
}

void pc_speech_format(const pc_speech_timer_t *t, char out[9])
{
    uint32_t sec;
    unsigned h;
    unsigned m;
    unsigned s;

    if (out == NULL) {
        return;
    }
    if (t == NULL) {
        /* 防御式降级:没有实例时显示零值,而不是残缺字符串。 */
        out[0] = '0';
        out[1] = '0';
        out[2] = ':';
        out[3] = '0';
        out[4] = '0';
        out[5] = '\0';
        return;
    }

    /* 钳制到显示上限(见文件头宏注释)。 */
    sec = t->sec <= PC_SPEECH_DISPLAY_MAX_SEC ? t->sec : PC_SPEECH_DISPLAY_MAX_SEC;

    if (sec < PC_SPEECH_HOUR_SEC) {
        /* "MM:SS":两位分钟左侧补零(00:00 .. 59:59)。 */
        m = sec / 60U;
        s = sec % 60U;
        snprintf(out, 9, "%02u:%02u", m, s);
    } else {
        /* "HH:MM:SS":满 1 小时后切换为带小时格式(任务 #47)。
         * 小时位上限 99 (与显示上限 99:59:59 一致), 两位左侧补零;
         * 8 字符 + '\0' = 9 字节, 与缓冲容量严格对齐。 */
        h = sec / 3600U;
        m = (sec % 3600U) / 60U;
        s = sec % 60U;
        snprintf(out, 9, "%02u:%02u:%02u", h, m, s);
    }
}