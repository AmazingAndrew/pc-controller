// main/pc_speech_timer.c
// 演讲计时器实现(接口说明见头文件)。
//
// 平台无关:仅依赖 C11 标准库,可被 host 测试直接编译。
#include "pc_speech_timer.h"

#include <stddef.h>
#include <stdio.h>

/* 显示上限:999 分 59 秒 = 60059 秒。
 * 依据:屏显缓冲固定 8 字节,"MMM:SS" + '\0' 恰好 7 + 1 = 8 字节,
 * 四位分钟装不下;演讲超过 16.7 小时无显示意义,直接钳制
 * (依据与取舍见头文件注释)。 */
#define PC_SPEECH_DISPLAY_MAX_SEC 60059U

void pc_speech_reset(pc_speech_timer_t *t)
{
    if (t == NULL) {
        return;
    }
    t->sec = 0U;
}

void pc_speech_tick(pc_speech_timer_t *t)
{
    if (t == NULL) {
        return;
    }
    /* 自然自增;32 位秒数理论 136 年回绕,不做饱和。 */
    t->sec++;
}

uint32_t pc_speech_seconds(const pc_speech_timer_t *t)
{
    return t != NULL ? t->sec : 0U;
}

void pc_speech_format(const pc_speech_timer_t *t, char out[8])
{
    uint32_t sec;
    unsigned min;
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
    min = (unsigned)(sec / 60U);
    s = (unsigned)(sec % 60U);

    if (min <= 99U) {
        /* "MM:SS":两位分钟左侧补零(00:00 .. 99:59)。 */
        snprintf(out, 8, "%02u:%02u", min, s);
    } else {
        /* "MMM:SS":三位分钟不再补零(100:00 .. 999:59),
         * 7 字符 + '\0' 恰好 8 字节。 */
        snprintf(out, 8, "%u:%02u", min, s);
    }
}
