// tests/test_pc_power_fsm.c
// 电源子状态机 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 §6 电源子状态机与 §1/FR-10:
//   ACTIVE --(15 s 空闲)--> DIM --(60 s)--> OFF --> LIGHT --> DEEP;
//   任意活动回 ACTIVE。默认阈值 15 / 60 / 300 / 1800;
// 阈值比较为 ">="(恰好到达即降档)。LIGHT / DEEP 用可注入的小阈值
// 测试,避免依赖 300/1800 大数值。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_power_fsm.c main/pc_power_fsm.c
#include <assert.h>
#include <stddef.h>

#include "pc_power_fsm.h"

int main(void)
{
    pc_power_fsm_t f;
    pc_power_cfg_t cfg;

    /* ======== 初始化 ======== */

    /* cfg = NULL:使用默认阈值 15 / 60 / 300 / 1800(规格 §1)。 */
    pc_power_init(&f, NULL);
    assert(f.state == PC_PW_ACTIVE);
    assert(f.idle_s == 0);
    assert(f.cfg.dim_s == 15);
    assert(f.cfg.off_s == 60);
    assert(f.cfg.light_s == 300);
    assert(f.cfg.deep_s == 1800);

    /* cfg 非空:值拷贝,初始化后可修改原配置不影响实例。 */
    cfg.dim_s = 2;
    cfg.off_s = 4;
    cfg.light_s = 8;
    cfg.deep_s = 16;
    pc_power_init(&f, &cfg);
    assert(f.state == PC_PW_ACTIVE);
    assert(f.cfg.dim_s == 2);
    cfg.dim_s = 99; /* 证明是拷贝而非引用 */
    assert(f.cfg.dim_s == 2);

    /* ======== 默认阈值边界(15 / 60)—— ">=" 语义 ======== */

    pc_power_init(&f, NULL);

    /* 14 s:未到 15 -> 保持 ACTIVE。 */
    assert(pc_power_elapsed(&f, 14) == PC_PW_ACTIVE);
    assert(f.state == PC_PW_ACTIVE);
    assert(f.idle_s == 14);

    /* 15 s:恰好到达 -> DIM。 */
    assert(pc_power_elapsed(&f, 15) == PC_PW_DIM);
    assert(f.state == PC_PW_DIM);

    /* 59 s:仍在 [15, 60) -> DIM。 */
    assert(pc_power_elapsed(&f, 59) == PC_PW_DIM);

    /* 60 s:恰好到达 -> OFF。 */
    assert(pc_power_elapsed(&f, 60) == PC_PW_OFF);
    assert(f.state == PC_PW_OFF);

    /* 默认 LIGHT / DEEP 边界:300 / 1800。 */
    assert(pc_power_elapsed(&f, 299) == PC_PW_OFF);
    assert(pc_power_elapsed(&f, 300) == PC_PW_LIGHT);
    assert(pc_power_elapsed(&f, 1799) == PC_PW_LIGHT);
    assert(pc_power_elapsed(&f, 1800) == PC_PW_DEEP);

    /* ======== 可注入小阈值:LIGHT / DEEP 边界 ======== */

    cfg.dim_s = 2;
    cfg.off_s = 4;
    cfg.light_s = 8;
    cfg.deep_s = 16;
    pc_power_init(&f, &cfg);

    assert(pc_power_elapsed(&f, 0) == PC_PW_ACTIVE);
    assert(pc_power_elapsed(&f, 1) == PC_PW_ACTIVE);
    assert(pc_power_elapsed(&f, 2) == PC_PW_DIM);
    assert(pc_power_elapsed(&f, 3) == PC_PW_DIM);
    assert(pc_power_elapsed(&f, 4) == PC_PW_OFF);
    assert(pc_power_elapsed(&f, 7) == PC_PW_OFF);
    assert(pc_power_elapsed(&f, 8) == PC_PW_LIGHT);
    assert(pc_power_elapsed(&f, 15) == PC_PW_LIGHT);
    assert(pc_power_elapsed(&f, 16) == PC_PW_DEEP);
    assert(pc_power_elapsed(&f, 1000) == PC_PW_DEEP);

    /* "应处即所处":按空闲时长直接给出档位,不依赖前一档位
     * (短时间空闲可以从深档直接回到高档位判定)。 */
    assert(pc_power_elapsed(&f, 3) == PC_PW_DIM);
    assert(pc_power_elapsed(&f, 0) == PC_PW_ACTIVE);

    /* ======== pc_power_activity:任意档位回 ACTIVE 且计时清零 ======== */

    pc_power_elapsed(&f, 1000); /* 先进深睡 */
    assert(f.state == PC_PW_DEEP);
    assert(pc_power_activity(&f) == PC_PW_ACTIVE);
    assert(f.state == PC_PW_ACTIVE);
    assert(f.idle_s == 0);

    /* 在任意档位上报活动都回 ACTIVE。 */
    pc_power_elapsed(&f, 2);
    assert(f.state == PC_PW_DIM);
    assert(pc_power_activity(&f) == PC_PW_ACTIVE);
    pc_power_elapsed(&f, 4);
    assert(f.state == PC_PW_OFF);
    assert(pc_power_activity(&f) == PC_PW_ACTIVE);

    /* ======== 防御式:NULL 实例 ======== */
    assert(pc_power_elapsed(NULL, 100) == PC_PW_ACTIVE);
    assert(pc_power_activity(NULL) == PC_PW_ACTIVE);
    pc_power_init(NULL, NULL); /* 不应崩溃 */

    return 0;
}
