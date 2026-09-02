// tests/test_pc_speech_timer.c
// 演讲计时器 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 §1/FR-05——进入演示模式清零(显示 00:00),按 1 Hz 走时;
// 任务 #47 后改为自适应格式,断言处注明:
//   - < 1 小时 (sec < 3600): "MM:SS" 左侧补零(如 65 s -> "01:05");
//   - >= 1 小时 (sec >= 3600): "HH:MM:SS" 左侧补零(如 3600 s -> "01:00:00");
//   - 上限钳制: 超出 99:59:59 的时长按 "99:59:59" 显示
//     (PC_SPEECH_DISPLAY_MAX_SEC = 359999,见 main/pc_speech_timer.c);
//     60059/60060 等 < 359999 的值不被钳制,按 HH:MM:SS 正常格式化。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_speech_timer.c main/pc_speech_timer.c
#include <assert.h>
#include <string.h>

#include "pc_speech_timer.h"

int main(void)
{
    pc_speech_timer_t t;
    char out[9];
    int i;

    /* 清零(进入演示模式,规格 §1/FR-05):读数 0,显示 "00:00"。 */
    pc_speech_reset(&t);
    assert(pc_speech_seconds(&t) == 0);
    pc_speech_format(&t, out);
    assert(strcmp(out, "00:00") == 0);

    /* 走 65 拍 -> 65 s = "01:05"(MM:SS 补零)。 */
    for (i = 0; i < 65; i++) {
        pc_speech_tick(&t);
    }
    assert(pc_speech_seconds(&t) == 65);
    pc_speech_format(&t, out);
    assert(strcmp(out, "01:05") == 0);

    /* 直接置秒数验证格式化(避免逐拍循环):59:59 边界。 */
    t.sec = 3599;
    pc_speech_format(&t, out);
    assert(strcmp(out, "59:59") == 0);

    /* 3600 s:任务 #47 自适应格式边界,HH:MM:SS 起点 -> "01:00:00"。 */
    t.sec = 3600;
    pc_speech_format(&t, out);
    assert(strcmp(out, "01:00:00") == 0);

    /* 59999 s:HH:MM:SS -> 16 h 39 m 59 s = "16:39:59"(任务 #47)。 */
    t.sec = 59999;
    pc_speech_format(&t, out);
    assert(strcmp(out, "16:39:59") == 0);

    /* 钳制上限 99:59:59 = 359999 s (PC_SPEECH_DISPLAY_MAX_SEC);
     * 60059 / 60060 < 359999,不被钳制,按 HH:MM:SS 正常格式化;
     * 仅 0xFFFFFFFF (4294967295) > 359999 被钳制到 "99:59:59"。 */
    t.sec = 60059; /* 16 h 40 m 59 s, 不钳制 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "16:40:59") == 0);
    t.sec = 60060; /* 16 h 41 m 0 s, 不钳制 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "16:41:00") == 0);
    t.sec = 0xFFFFFFFFU; /* 钳制到 99:59:59 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "99:59:59") == 0);

    /* 60000 s:HH:MM:SS -> 16 h 40 m 0 s = "16:40:00"(任务 #47)。 */
    t.sec = 60000;
    pc_speech_format(&t, out);
    assert(strcmp(out, "16:40:00") == 0);

    /* 常规走时连续性:9:59 + 1 拍 -> 10:00。 */
    t.sec = 599;
    pc_speech_tick(&t);
    pc_speech_format(&t, out);
    assert(strcmp(out, "10:00") == 0);

    /* 防御式降级:实例为 NULL 时读数 0 / 显示 "00:00"。 */
    assert(pc_speech_seconds(NULL) == 0);
    pc_speech_format(NULL, out);
    assert(strcmp(out, "00:00") == 0);

    /* 清零对非零实例生效。 */
    t.sec = 12345;
    pc_speech_reset(&t);
    assert(pc_speech_seconds(&t) == 0);

    return 0;
}
