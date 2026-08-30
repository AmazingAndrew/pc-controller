// main/pc_power_fsm.c
// 电源子状态机实现(接口说明见头文件)。
//
// 平台无关:仅依赖 C11 标准库,可被 host 测试直接编译。
#include "pc_power_fsm.h"

#include <stddef.h>

/* 默认阈值(秒)。出处:规格 §6 电源子状态机
//   "ACTIVE --(15 s idle)--> DIM --(60 s idle)--> SCREEN OFF
//    --> LIGHT SLEEP --> DEEP SLEEP"
// 以及 §1/FR-10(变暗 15 s、灭屏 60 s、随后浅睡,最终深睡)。
// 浅睡取 300 s(灭屏后 4 分钟)、深睡取 1800 s(30 分钟):
// 规格未固定这两个数值,此处在"待机 0.2-1 mA、深睡 20-40 uA"
// 的功耗目标下取保守折中,并开放为可配置项(见头文件)。 */
#define PC_PW_DEFAULT_DIM_S   15U
#define PC_PW_DEFAULT_OFF_S   60U
#define PC_PW_DEFAULT_LIGHT_S 300U
#define PC_PW_DEFAULT_DEEP_S  1800U

void pc_power_init(pc_power_fsm_t *f, const pc_power_cfg_t *cfg)
{
    if (f == NULL) {
        return;
    }
    f->state = PC_PW_ACTIVE;
    f->idle_s = 0U;
    if (cfg != NULL) {
        f->cfg = *cfg; /* 值拷贝:调用方可在初始化后释放/修改原配置 */
    } else {
        f->cfg.dim_s = PC_PW_DEFAULT_DIM_S;
        f->cfg.off_s = PC_PW_DEFAULT_OFF_S;
        f->cfg.light_s = PC_PW_DEFAULT_LIGHT_S;
        f->cfg.deep_s = PC_PW_DEFAULT_DEEP_S;
    }
}

pc_power_t pc_power_activity(pc_power_fsm_t *f)
{
    if (f == NULL) {
        return PC_PW_ACTIVE;
    }
    /* 活动上报:无条件回 ACTIVE 并清零空闲计数。 */
    f->state = PC_PW_ACTIVE;
    f->idle_s = 0U;
    return PC_PW_ACTIVE;
}

pc_power_t pc_power_elapsed(pc_power_fsm_t *f, uint32_t idle_seconds)
{
    pc_power_t next;

    if (f == NULL) {
        return PC_PW_ACTIVE;
    }

    /* 从高到低逐级判定:取空闲时长已满足的最深档位。
     * ">=" 边界语义见头文件。 */
    if (idle_seconds >= f->cfg.deep_s) {
        next = PC_PW_DEEP;
    } else if (idle_seconds >= f->cfg.light_s) {
        next = PC_PW_LIGHT;
    } else if (idle_seconds >= f->cfg.off_s) {
        next = PC_PW_OFF;
    } else if (idle_seconds >= f->cfg.dim_s) {
        next = PC_PW_DIM;
    } else {
        next = PC_PW_ACTIVE;
    }

    f->idle_s = idle_seconds;
    f->state = next;
    return next;
}
