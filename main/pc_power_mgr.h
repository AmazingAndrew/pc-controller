// main/pc_power_mgr.h
// PC Controller 平台层:电源管理器(把纯逻辑电源状态机落到硬件)。
//
// 职责:持有 pc_power_fsm_t 实例做"空闲时长 -> 档位"决策,并把
// 决策翻译成平台动作:
//   ACTIVE -> DIM          背光降档(默认 30%,档位可配置)
//   DIM    -> OFF          背光关闭(MCU 继续运行,应用态进 SLEEP)
//   OFF    -> LIGHT SLEEP  esp_light_sleep_start() 周期循环
//   LIGHT  -> DEEP SLEEP   esp_deep_sleep_start()(芯片冷重启)
// 档位链与阈值事实源:规格 §1/FR-10 与 §6 电源子状态机
//   "ACTIVE --(15 s idle)--> DIM --(60 s idle)--> SCREEN OFF
//    --> LIGHT SLEEP --> DEEP SLEEP;任意键唤醒(首键吞掉)"。
// 默认阈值 15/60/300/1800 秒出处:规格 §6 + pc_power_fsm.c 头注释
// (浅睡/深睡两档规格未固定数值,取功耗目标下的保守折中)。
//
// 唤醒源与深睡降级(规格 §10 行 195 / §15 开放问题):
//   GPIO0 按键唤醒路径在本板未验证(§15 必测项),故本模块默认
//   不配置 GPIO 唤醒,睡眠档位的退出依赖两条降级路径:
//     路径①(默认,编译开关 PC_PM_DEEP_SLEEP_ENABLE=0):
//        浅睡常驻 + 定时唤醒开广播窗口(规格 §10 "(a) stay in
//        light sleep with periodic advertising windows");空闲到达
//        深睡阈值时也不真正深睡,继续浅睡循环——保证设备始终可被
//        主机回连"唤醒",不会因无法唤醒而变砖。
//     路径②(编译开关 PC_PM_DEEP_SLEEP_ENABLE=1):
//        真正 esp_deep_sleep_start(),不配唤醒源,退出依赖独立硬件
//        电源键整机重启(规格 §10 "(b) rely on the independent
//        hardware power button");深睡唤醒 = 芯片冷启动,
//        app_main 重跑,无"首键"问题。
//   默认选路径①的理由:路径②在电源键行为未确认的板上会让设备
//   进入"无人可唤醒"状态;路径①以少量待机电流换回连可达性,
//   与规格待机功耗目标(0.2-1 mA)同数量级兼容。
//
// 首键吞掉:OFF/浅睡档位的"首键只唤醒不执行功能"由组装层把应用
// 状态机切入 PC_ST_SLEEP(置 wake_key_pending)后经按键语义层
// PC_ACT_WAKE 覆盖;深睡唤醒为冷启动,不适用(见组装层注释)。
//
// 定时器策略(规格 §7 行 163):两个 esp_timer 单次定时器承担
// 15 s 变暗 / 60 s 熄屏的背光超时(回调仅写 LEDC,无 UI 操作);
// 浅睡/深睡档位的 1 s 粒度决策不开新常驻定时器,由组装层已有的
// 1 Hz 事件顺带调 pc_power_mgr_tick_1s() 驱动。
//
// 线程上下文:
//   - init / activity / tick_1s / 各 setter:应用任务调用;
//   - 通知回调(见 pc_pm_ev_t)恒在应用任务上下文发出
//     (activity() 与 tick_1s() 都由应用任务调用);
//   - 两个背光定时器回调在 esp_timer 任务,只写背光 LEDC(幂等、
//     无共享状态竞争);睡眠标志/档位状态/通知一律归应用任务所有,
//     由 tick_1s 在 1 s 内对齐。
// 内存所有权:不分配堆内存;实例状态全部为文件内静态存储。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 深睡降级路径编译开关(事实源:规格 §10/§15,详见文件头)。
 * 0 = 路径①浅睡常驻(默认);1 = 路径②真深睡 + 硬件电源键唤醒。 */
#ifndef PC_PM_DEEP_SLEEP_ENABLE
#define PC_PM_DEEP_SLEEP_ENABLE 0
#endif

/* GPIO0 按键唤醒编译开关(默认关):本板唤醒路径未验证
 * (规格 §10/§15 必测项)。实测通过后可置 1 让睡眠档位支持
 * "任意键唤醒";置 1 的代码路径同样保留规格引用注释。 */
#ifndef PC_PM_GPIO_WAKE_ENABLE
#define PC_PM_GPIO_WAKE_ENABLE 0
#endif

/* 电源管理器向组装层发出的通知事件。
 * 回调恒在应用任务上下文执行(见文件头线程上下文),组装层可
 * 直接做 FSM/UI/BLE 操作。 */
typedef enum {
    /* 灭屏到达(60 s 空闲):组装层可立即把应用态切 SLEEP;
     * 当前组装层改用事件循环顶部的惰性同步(见 pc_app_main.c),
     * 本事件保留供需要即时处理的场景。 */
    PC_PM_EV_OFF = 0,
    /* 从睡眠档位(灭屏/浅睡循环)被活动唤醒:背光已还原,
     * 组装层按需刷新页面(唤醒键本身的页面刷新走按键处理路径)。 */
    PC_PM_EV_WAKE,
    /* 即将进入浅睡循环:背光已关,组装层应把应用态切 SLEEP、
     * 停掉可能访问 UI 的反馈定时器(页面退出顺序,规格 §7 行 164)。 */
    PC_PM_EV_LIGHT_ENTER,
    /* 浅睡循环的周期性定时唤醒窗口:组装层可开一段广播窗口
     * (规格 §10 降级路径①:periodic advertising windows)。 */
    PC_PM_EV_LIGHT_WINDOW,
    /* 即将深睡(仅路径②):芯片随后冷重启,组装层应停无线
     * (pc_ble_hid_stop() 即为此路径保留,见 pc_ble_hid.h)。 */
    PC_PM_EV_DEEP_ENTER,
} pc_pm_ev_t;

typedef void (*pc_pm_notify_t)(pc_pm_ev_t ev);

/* 初始化电源管理器。
 * 行为:以默认阈值 15/60/300/1800(规格 §6)初始化内部电源
 *      状态机;创建 15 s 变暗 / 60 s 熄屏两个单次定时器并启动
 *      (定时器创建失败仅日志——1 s 决策路径可兜底,规格 §10);
 *      检查 RTC 保留区判定是否深睡唤醒回启(路径②,诊断日志)。
 * 失败值:无(定时器失败内部降级)。
 * 线程上下文:启动阶段,应用任务(此时队列/按键回调尚未建立)。 */
void pc_power_mgr_init(void);

/* 注册电源事件通知回调(组装层接线点,见 pc_pm_ev_t)。
 * 允许 NULL(撤销通知)。重复注册以最后一次为准。
 * 说明:任务契约的四接口不含通知,但"进入前关背光、恢复后还原
 * 页面由组装层刷新"(规格接线要求)必须有一条平台层 -> 组装层
 * 的事件通路,故追加本注册接口;回调恒在应用任务上下文发出。 */
void pc_power_mgr_set_notify(pc_pm_notify_t cb);

/* 设置 ACTIVE 档背光百分比(唤醒/活动时还原用)。
 * 参数:percent 0..100(取自配置项 `pp_cfg.backlight`,规格 §8);
 *      越界钳制。默认 100。
 * 线程上下文:应用任务;非阻塞。 */
void pc_power_mgr_set_active_backlight(uint8_t percent);

/* 记录当前槽位上下文(深睡前写入 RTC 保留区,冷启动后诊断用)。
 * 参数:slot 0..2;越界按 0 处理。组装层在初始化与槽位切换后调用。
 * 线程上下文:应用任务;非阻塞。 */
void pc_power_mgr_set_slot_ctx(uint8_t slot);

/* 上报一次用户活动(任意有效用户事件:按键/连接事件等)。
 * 行为:电源状态机回 ACTIVE、空闲清零;停掉背光超时定时器并重启;
 *      若此前处于灭屏档,还原背光到活动档并(经回调)通知
 *      PC_PM_EV_WAKE;若处于浅睡循环,置退出请求(循环退出时
 *      统一还原)。
 * 返回值:无。失败值:无。
 * 线程上下文:应用任务;无阻塞、无分配。 */
void pc_power_mgr_activity(void);

/* 1 s 决策:累加空闲秒数,经 pc_power_elapsed() 决策并执行档位
 * 变化(背光降档/灭屏置睡眠标志/进浅睡循环/进深睡)。
 * 注意:进入浅睡循环时本函数会阻塞(直到被活动请求退出或转入
 * 深睡),期间整个 CPU 随 esp_light_sleep 停摆——只允许应用任务
 * 调用;组装层已有 1 Hz 事件顺带驱动,不新增常驻定时器。
 * 返回值:无。失败值:无(浅睡唤醒源配置失败时降级为灭屏常驻)。
 * 线程上下文:应用任务。 */
void pc_power_mgr_tick_1s(void);

/* 是否处于睡眠档(灭屏或更深)。
 * 组装层事件循环据此把应用状态机惰性同步到 PC_ST_SLEEP,使
 * "唤醒首键只唤醒不执行功能"(规格 §6 转移表最后一行)生效。
 * 线程上下文:任意(只读标志);无阻塞。 */
bool pc_power_mgr_is_sleeping(void);

/* 深睡唤醒诊断:上次运行经路径②进入深睡的次数累计(0 = 无有效
 * RTC 上下文)。仿 demo_low_power.c 行 65-71 的 magic/计数模式。
 * 线程上下文:任意(只读);无阻塞。 */
uint32_t pc_power_mgr_deep_wake_count(void);
