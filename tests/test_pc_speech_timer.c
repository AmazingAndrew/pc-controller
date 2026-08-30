// tests/test_pc_speech_timer.c
// 演讲计时器 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 §1/FR-05——进入演示模式清零(显示 00:00),按 1 Hz 走时。
// 格式化策略以实现为准并在断言处注明:
//   - 分钟 <= 99 时 "MM:SS" 左侧补零;
//   - 分钟 >= 100 时 "MMM:SS" 三位分钟不补零(如 3600 s -> "60:00");
//   - 钳制行为以实现为准(与头文件注释存在差异,见报告):
//     实现的钳制常量 PC_SPEECH_DISPLAY_MAX_SEC = 60059,头文件注释称其等于
//     "999 分 59 秒",但 999:59 实际 = 59999 s;60059 s 格式化为 "1000:59"
//     (7 字符 + '\0' 恰好装得下 8 字节缓冲,未发生截断)。本测试按实现的
//     真实行为断言,差异已在任务报告中记录,不改模块。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_speech_timer.c main/pc_speech_timer.c
#include <assert.h>
#include <string.h>

#include "pc_speech_timer.h"

int main(void)
{
    pc_speech_timer_t t;
    char out[8];
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

    /* 3600 s:实现策略为分钟三位不补零的 "MMM:SS" -> "60:00"。 */
    t.sec = 3600;
    pc_speech_format(&t, out);
    assert(strcmp(out, "60:00") == 0);

    /* 真正的三位分钟上界:999:59 = 59999 s(注意不是头文件注释的 60059)。 */
    t.sec = 59999;
    pc_speech_format(&t, out);
    assert(strcmp(out, "999:59") == 0);

    /* 钳制行为(以实现为准,差异见文件头注释):
     * 钳制常量为 60059,格式化为 "1000:59" 而非头文件所述的 "999:59";
     * "1000:59" = 7 字符 + '\0' = 8 字节,恰好装得下,不发生截断。 */
    t.sec = 60059; /* 钳制边界本身 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "1000:59") == 0);
    t.sec = 60060; /* 超界被钳制到 60059 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "1000:59") == 0);
    t.sec = 0xFFFFFFFFU; /* 理论极限同样钳制 */
    pc_speech_format(&t, out);
    assert(strcmp(out, "1000:59") == 0);

    /* 钳制区间内不受影响:60000 s -> "1000:00"。 */
    t.sec = 60000;
    pc_speech_format(&t, out);
    assert(strcmp(out, "1000:00") == 0);

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
