// main/pc_power_fsm.h
// 电源子状态机(平台无关纯逻辑模块)。
//
// 职责:按"自上次活动以来的空闲时长"决策电源档位,供平台层
// 执行对应的背光/睡眠动作。档位链与超时阈值来自规格 §1/FR-10
// 与 §6 电源子状态机:
//
//   ACTIVE --(dim_s 空闲)--> DIM --(off_s 空闲)--> OFF
//          --(light_s 空闲)--> LIGHT --(deep_s 空闲)--> DEEP
//   任意按键唤醒 -> ACTIVE(首键被吞,吞键逻辑在应用状态机
//   pc_app_fsm 的 wake_key_pending 中处理)。
//
// 默认阈值(秒,规格 §1/§6):变暗 15 / 灭屏 60 / 浅睡 300 / 深睡 1800。
// 阈值可配置(未来配置项,规格 §8 `pp_cfg`),本模块不做持久化。
//
// 分工:本模块只做"纯决策"(给定空闲时长 -> 应处档位);
// 实际空闲累计、背光 PWM、esp_light_sleep/esp_deep_sleep 进入与
// 唤醒源配置都在平台层。深睡唤醒路径的板级验证状态见规格 §15。
//
// 平台无关性:仅依赖 C11 标准库,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 电源档位。按功耗从低到高排列仅表达枚举语义,
 * 不做数值比较(决策逻辑显式比较各阈值)。 */
typedef enum {
    PC_PW_ACTIVE = 0, /* 活跃:全亮背光,全部功能可用 */
    PC_PW_DIM,        /* 变暗:背光降一档,功能不受影响 */
    PC_PW_OFF,        /* 灭屏:背光关闭,MCU 仍正常运行 */
    PC_PW_LIGHT,      /* 浅睡:esp_light_sleep 级,广播窗口降占空比 */
    PC_PW_DEEP,       /* 深睡:esp_deep_sleep 级,20-40 uA(规格 §1) */
} pc_power_t;

/* 电源阈值配置(单位:秒,全部为"自上次活动起的空闲时长")。
 * 默认值来自规格 §6 电源子状态机与 §1/FR-10:
 *   dim_s   = 15   变暗
 *   off_s   = 60   灭屏
 *   light_s = 300  浅睡
 *   deep_s  = 1800 深睡
 * 约束:调用方应保证 dim <= off <= light <= deep;
 * 本模块对乱序配置按"逐阈值独立比较"处理,结果等价于取
 * 已满足的最高档,不会产生未定义行为。 */
typedef struct {
    uint32_t dim_s;   /* 空闲达该秒数 -> DIM */
    uint32_t off_s;   /* 空闲达该秒数 -> OFF */
    uint32_t light_s; /* 空闲达该秒数 -> LIGHT */
    uint32_t deep_s;  /* 空闲达该秒数 -> DEEP */
} pc_power_cfg_t;

/* 电源子状态机实例。由组装层持有(应用任务内独占读写)。 */
typedef struct {
    pc_power_t state;  /* 当前档位 */
    /* 最近一次上报的空闲时长(秒),即"自上次活动起"的累计值,
     * 由平台层维护并周期性喂入;本字段仅供调试/屏显参考。 */
    uint32_t idle_s;
    pc_power_cfg_t cfg; /* 阈值配置(初始化时拷贝) */
} pc_power_fsm_t;

/* 初始化电源子状态机。
 * 参数:
 *   f   实例,不可为 NULL(为 NULL 时直接返回)。
 *   cfg 阈值配置;传 NULL 时使用默认 15/60/300/1800(规格 §1)。
 * 副作用:档位置 ACTIVE,空闲计数清零,拷贝配置。
 * 线程上下文:启动阶段单线程调用。内存所有权:不分配内存。 */
void pc_power_init(pc_power_fsm_t *f, const pc_power_cfg_t *cfg);

/* 上报一次用户活动(任意按键/连接事件等)。
 * 行为:档位回到 ACTIVE,空闲计数清零。
 * 返回值:新档位(恒为 PC_PW_ACTIVE)。
 * 参数/线程上下文:同 pc_power_init;无阻塞。 */
pc_power_t pc_power_activity(pc_power_fsm_t *f);

/* 按空闲时长做纯决策。
 * 参数:
 *   f           实例,不可为 NULL(为 NULL 时返回 PC_PW_ACTIVE)。
 *   idle_seconds 自上次活动起的累计空闲秒数(由平台层维护)。
 * 行为:
 *   - 更新内部空闲读数与档位;
 *   - 阈值比较为">=":恰好到达阈值即降档(边界语义与定时器
 *     周期对齐,1 s 粒度);
 *   - 从高功耗档位向低功耗档位的降级由本函数自动完成;
 *     升档(唤醒)只能通过 pc_power_activity;本函数始终
 *     "应处即所处"——按空闲时长直接给出档位,不受前一档位影响。
 * 返回值:该空闲时长下应处的档位。
 * 线程上下文:应用任务(定时器回调归一后);无阻塞,无分配。 */
pc_power_t pc_power_elapsed(pc_power_fsm_t *f, uint32_t idle_seconds);
