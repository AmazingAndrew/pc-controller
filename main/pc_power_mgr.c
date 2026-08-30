// main/pc_power_mgr.c
// 电源管理器实现(接口说明见头文件)。
//
// 分层纪律:本模块是平台层,只依赖 BSP(背光)与 ESP-IDF 睡眠
// API + 纯逻辑模块 pc_power_fsm;不触碰应用状态机/按键语义/
// BLE/UI——那些接线由组装层 (pc_app_main.c) 经通知回调完成。
#include "pc_power_mgr.h"

#include "pc_power_fsm.h"

#include "bsp_display.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pc_power";

/* ---- 档位执行常量 ---- */

/* DIM 档背光百分比。规格 §6 只说"降一档"(dim one step),未定
 * 数值;取 30%(在 LEDC 8 bit 调光下肉眼明显变暗且不灭屏)。
 * 可配置化(未来并入 `pp_cfg`,规格 §8)时改此常量或经参数注入。 */
#define PC_PM_DIM_LEVEL 30U

/* 浅睡循环参数(降级路径①,规格 §10 "(a) stay in light sleep with
 * periodic advertising windows"):
 *   睡眠段 30 s —— CPU 停摆,电流介于活动与深睡之间;
 *   唤醒窗口 5 s —— 留给组装层开广播窗口并让主机有机会回连
 *   (窗口时长与广播占空比需真机实测,规格 §13 验收项)。 */
#define PC_PM_LIGHT_SLEEP_S  30U
#define PC_PM_LIGHT_WINDOW_S 5U

/* ---- 深睡 RTC 保留上下文(仿 demo_low_power.c 行 65-71) ---- */

/* magic:验证 RTC 保留区内容有效(首次上电为随机/零值)。 */
#define PC_PM_DEEP_MAGIC 0x5043504DUL /* ASCII "PCPM" */

static RTC_DATA_ATTR uint32_t s_deep_magic; /* 有效 = 上次经深睡退出 */
static RTC_DATA_ATTR uint32_t s_deep_count; /* 深睡进入次数累计 */
static RTC_DATA_ATTR uint8_t  s_deep_slot;  /* 深睡时的当前槽位 */

/* ---- 文件内状态(除注明外均由应用任务独占读写) ---- */

static pc_power_fsm_t s_fsm;          /* 纯逻辑电源状态机实例 */
static uint32_t s_idle_s;             /* 自上次活动起的空闲秒数 */
static pc_power_t s_applied;          /* 已执行的档位(防重复执行) */
static bool s_sleeping;               /* 睡眠档标志(灭屏或更深) */
static bool s_in_light_cycle;         /* 浅睡循环进行中 */
static volatile bool s_light_exit_req;/* 浅睡循环退出请求(防御性,
                                       * 见 pc_power_mgr_activity 注释) */
static uint8_t s_active_bl = 100;     /* ACTIVE 档背光(来自配置) */
static uint8_t s_slot_ctx;            /* 当前槽位(深睡上下文) */
static pc_pm_notify_t s_notify;       /* 组装层通知回调 */

static esp_timer_handle_t s_dim_timer; /* 15 s 变暗,单次(规格 §7) */
static esp_timer_handle_t s_off_timer; /* 60 s 熄屏,单次(规格 §7) */

/* ---- 前向声明 ---- */
static void restart_backlight_timers(void);
static void light_sleep_cycle(void);
#if PC_PM_DEEP_SLEEP_ENABLE
static void deep_sleep_enter(void); /* 仅路径②编译(见定义处注释) */
#endif

/* ---- 背光超时定时器回调(仅硬件动作) ----
 * 运行在 esp_timer 任务。只写背光(幂等),不修改共享状态——
 * 档位状态由应用任务的 tick_1s 在 1 s 内对齐,避免任务间竞争。
 * 定时器存在的意义:让变暗/熄屏精确落在 15 s / 60 s,而不是等
 * 下一个 1 s 决策拍(规格 §7 行 163 的"两个背光超时")。 */
static void on_dim_timer(void *arg)
{
    (void)arg;
    bsp_display_backlight(PC_PM_DIM_LEVEL); /* DIM 档:降一档 */
}

static void on_off_timer(void *arg)
{
    (void)arg;
    bsp_display_backlight(0U); /* OFF 档:灭屏 */
}

/* 停掉并重启两个背光超时定时器(活动上报时调用)。
 * 时长取自电源状态机配置(默认 15/60,规格 §6)。
 * 定时器未创建成功(初始化降级)时静默跳过——1 s 决策路径兜底。 */
static void restart_backlight_timers(void)
{
    if (s_dim_timer != NULL) {
        esp_timer_stop(s_dim_timer);
        esp_timer_start_once(s_dim_timer,
                             (uint64_t)s_fsm.cfg.dim_s * 1000000ULL);
    }
    if (s_off_timer != NULL) {
        esp_timer_stop(s_off_timer);
        esp_timer_start_once(s_off_timer,
                             (uint64_t)s_fsm.cfg.off_s * 1000000ULL);
    }
}

/* ---- 深睡(路径②) ----
 * 显式声明:GPIO0 按键深睡唤醒未在本板验证(规格 §10 行 195 /
 * §15 必测项),因此【不配置任何唤醒源】——深睡退出的两条降级
 * 路径见头文件;本函数对应路径②(依赖硬件电源键开机 = 冷启动)。
 * 不返回:esp_deep_sleep_start() 成功后芯片冷重启,app_main 重跑。
 * 仅在路径②编译开关打开时编译,避免路径①下未引用告警。 */
#if PC_PM_DEEP_SLEEP_ENABLE
static void deep_sleep_enter(void)
{
    if (s_notify != NULL) {
        s_notify(PC_PM_EV_DEEP_ENTER); /* 组装层:停无线等收尾 */
    }

    /* RTC 保留上下文:magic/计数模式(仿 demo_low_power.c:65-71) */
    if (s_deep_magic != PC_PM_DEEP_MAGIC) {
        s_deep_count = 0U; /* RTC 区无效(首启/掉电),重新计数 */
    }
    s_deep_magic = PC_PM_DEEP_MAGIC;
    s_deep_count++;
    s_deep_slot = s_slot_ctx;

    bsp_display_backlight(0U);
    ESP_LOGI(TAG, "entering deep sleep (fallback path 2): cycle #%lu, "
                  "wake requires hardware power button (GPIO0 wake "
                  "unverified, spec S10/S15)",
             (unsigned long)s_deep_count);
    esp_deep_sleep_start();
    /* 不可达:若异常返回,按灭屏常驻降级,等待下一次 1 s 决策。 */
    ESP_LOGE(TAG, "deep sleep start returned unexpectedly");
}
#endif /* PC_PM_DEEP_SLEEP_ENABLE */

/* ---- 浅睡循环(默认降级路径①) ----
 * 复用 demo_low_power.c 行 77-87 的 light sleep 范式:
 * 进睡前关背光;唤醒源为 RTC 定时器(esp_light_sleep 期间 CPU
 * 停摆、esp_timer 不走,按键因 GPIO 唤醒未验证【不】作为唤醒源,
 * 规格 §10/§15);唤醒窗口内由组装层经通知开广播。
 * 运行于应用任务;循环期间整个任务(乃至 CPU)随睡眠停摆,
 * 队列事件在唤醒/退出后统一处理。
 * 退出途径:① 活动请求(防御路径,见 activity 注释);
 *          ② 唤醒源配置失败 -> 降级为灭屏常驻(规格 §10);
 *          ③ 路径②下空闲达深睡阈值 -> 转入深睡(不返回)。 */
static void light_sleep_cycle(void)
{
    s_in_light_cycle = true;
    s_sleeping = true;
    s_applied = PC_PW_LIGHT;
    s_light_exit_req = false;

    bsp_display_backlight(0U);          /* 进入前关背光(接线契约) */
    esp_timer_stop(s_dim_timer);        /* 睡眠期间背光超时无意义 */
    esp_timer_stop(s_off_timer);

    if (s_notify != NULL) {
        s_notify(PC_PM_EV_LIGHT_ENTER); /* 组装层:应用态切 SLEEP、
                                         * 停反馈定时器(页面退出顺序) */
    }

    ESP_LOGI(TAG, "light sleep cycle started (fallback path 1: "
                  "periodic advertising windows, spec S10)");

    while (!s_light_exit_req) {
#if PC_PM_GPIO_WAKE_ENABLE
        /* GPIO0 按键唤醒:本板未验证(规格 §10/§15 必测项)。
         * 实测通过后将 PC_PM_GPIO_WAKE_ENABLE 置 1 即可让睡眠
         * 档位支持"任意键唤醒,首键吞掉"的完整规格语义。 */
        (void)esp_sleep_enable_gpio_wakeup(1ULL << 0); /* GPIO0 */
#endif
        esp_err_t err = esp_sleep_enable_timer_wakeup(
            (uint64_t)PC_PM_LIGHT_SLEEP_S * 1000000ULL);
        if (err != ESP_OK) {
            /* 唤醒源配置失败:降级为灭屏常驻(屏幕已关、MCU 继续
             * 运行,按键仍可唤醒),不阻塞功能(规格 §10)。 */
            ESP_LOGE(TAG, "light sleep wakeup cfg failed: %s; "
                          "degrading to screen-off residency",
                     esp_err_to_name(err));
            s_applied = PC_PW_OFF;
            break;
        }

        /* CPU 停摆;RTC 定时到点后自此处继续(范式同
         * demo_low_power.c:84)。 */
        (void)esp_light_sleep_start();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

        /* 睡眠段近似计入空闲(唤醒窗口随后还会累加窗口时长)。 */
        s_idle_s += PC_PM_LIGHT_SLEEP_S;
        (void)pc_power_elapsed(&s_fsm, s_idle_s); /* 同步诊断读数 */

        if (s_light_exit_req) break;

        /* 周期性唤醒窗口:组装层开广播(降级路径①核心)。 */
        if (s_notify != NULL) {
            s_notify(PC_PM_EV_LIGHT_WINDOW);
        }
        for (uint32_t i = 0U; i < PC_PM_LIGHT_WINDOW_S; i++) {
            if (s_light_exit_req) break;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        s_idle_s += PC_PM_LIGHT_WINDOW_S;

#if PC_PM_DEEP_SLEEP_ENABLE
        /* 路径②:空闲达深睡阈值 -> 真深睡(不返回)。 */
        if (!s_light_exit_req && s_idle_s >= s_fsm.cfg.deep_s) {
            deep_sleep_enter();
        }
#else
        /* 路径①(默认):深睡阈值到达也不深睡——GPIO0 唤醒未验证,
         * 继续浅睡循环保持"可被主机回连唤醒"(规格 §10/§15)。 */
        (void)0;
#endif
    }

    /* 退出循环:还原 ACTIVE(背光 + 定时器 + 状态)。 */
    s_in_light_cycle = false;
    s_light_exit_req = false;
    s_sleeping = false;
    s_applied = PC_PW_ACTIVE;
    s_idle_s = 0U;
    (void)pc_power_activity(&s_fsm);
    bsp_display_backlight(s_active_bl); /* 恢复后页面由组装层刷新 */
    restart_backlight_timers();
    if (s_notify != NULL) {
        s_notify(PC_PM_EV_WAKE);
    }
    ESP_LOGI(TAG, "light sleep cycle exited, back to ACTIVE");
}

/* ---- 对外接口 ---- */

void pc_power_mgr_init(void)
{
    /* 纯逻辑电源状态机:默认阈值 15/60/300/1800(规格 §6/§1;
     * 浅睡/深睡取值依据见 pc_power_fsm.c 头注释)。 */
    pc_power_init(&s_fsm, NULL);
    s_idle_s = 0U;
    s_applied = PC_PW_ACTIVE;
    s_sleeping = false;
    s_in_light_cycle = false;
    s_light_exit_req = false;

    /* 深睡回启诊断:RTC 保留区有效说明上次运行以深睡结束
     * (路径②;深睡唤醒 = 冷启动,首键吞掉不适用——组装层注释)。 */
    if (s_deep_magic == PC_PM_DEEP_MAGIC) {
        ESP_LOGI(TAG, "resumed after deep sleep (RTC ctx valid): "
                      "cycle #%lu, slot %u",
                 (unsigned long)s_deep_count, (unsigned)s_deep_slot);
    }

    /* 两个背光超时定时器(规格 §7 行 163)。创建失败仅日志:
     * 1 s 决策路径按同一阈值兜底执行,功能不丢(规格 §10)。 */
    const esp_timer_create_args_t dim_args = { .callback = on_dim_timer,
                                               .name = "pc_pm_dim" };
    const esp_timer_create_args_t off_args = { .callback = on_off_timer,
                                               .name = "pc_pm_off" };
    if (esp_timer_create(&dim_args, &s_dim_timer) != ESP_OK) {
        s_dim_timer = NULL;
        ESP_LOGW(TAG, "dim timer create failed; 1 s decision covers it");
    }
    if (esp_timer_create(&off_args, &s_off_timer) != ESP_OK) {
        s_off_timer = NULL;
        ESP_LOGW(TAG, "off timer create failed; 1 s decision covers it");
    }
    restart_backlight_timers();

    ESP_LOGI(TAG, "ready: dim=%lus off=%lus light=%lus deep=%lus "
                  "(deep path %s)",
             (unsigned long)s_fsm.cfg.dim_s, (unsigned long)s_fsm.cfg.off_s,
             (unsigned long)s_fsm.cfg.light_s,
             (unsigned long)s_fsm.cfg.deep_s,
             PC_PM_DEEP_SLEEP_ENABLE ? "2: real deep sleep"
                                     : "1: light-sleep residency");
}

void pc_power_mgr_set_notify(pc_pm_notify_t cb)
{
    s_notify = cb; /* 应用任务上下文写入;回调发出点见各调用处 */
}

void pc_power_mgr_set_active_backlight(uint8_t percent)
{
    s_active_bl = percent; /* 0..100,uint8_t 天然钳制 */
}

void pc_power_mgr_set_slot_ctx(uint8_t slot)
{
    s_slot_ctx = (slot < 3U) ? slot : 0U; /* 3 槽防御(规格 §1) */
}

void pc_power_mgr_activity(void)
{
    bool was_sleeping = s_sleeping;

    (void)pc_power_activity(&s_fsm); /* 回 ACTIVE、清空闲 */
    s_idle_s = 0U;
    s_applied = PC_PW_ACTIVE;

    if (s_in_light_cycle) {
        /* 防御路径:浅睡循环运行在应用任务自身,正常情况下活动
         * 事件要等循环退出才会被处理;此分支覆盖未来在唤醒窗口
         * 内(如连接建立)主动退出的接线。还原动作由循环尾部
         * 统一完成,这里只置请求,避免重复还原。 */
        s_light_exit_req = true;
        return;
    }

    bsp_display_backlight(s_active_bl); /* 从灭屏/变暗还原 */
    restart_backlight_timers();         /* 背光超时重新计时 */
    if (was_sleeping && s_notify != NULL) {
        s_notify(PC_PM_EV_WAKE);
    }
}

void pc_power_mgr_tick_1s(void)
{
    if (s_in_light_cycle) return; /* 防御:循环中不重复决策 */

    s_idle_s++;
    /* "应处即所处":按累计空闲直接决策档位(边界 ">=",与
     * 15 s/60 s 定时器周期对齐),见 pc_power_fsm.h。 */
    pc_power_t st = pc_power_elapsed(&s_fsm, s_idle_s);
    if (st == s_applied) return; /* 无档位变化(重复执行幂等也省) */

    switch (st) {
    case PC_PW_DIM:
        /* 与 15 s 定时器同一动作(幂等):定时器给精度,这里给
         * 状态一致性。 */
        s_applied = st;
        bsp_display_backlight(PC_PM_DIM_LEVEL);
        break;

    case PC_PW_OFF:
        /* 灭屏:置睡眠标志,组装层事件循环惰性把应用态切
         * PC_ST_SLEEP(首键吞掉前置,规格 §6 转移表最后一行)。 */
        s_applied = st;
        s_sleeping = true;
        bsp_display_backlight(0U);
        ESP_LOGI(TAG, "screen off (idle %lus); app SLEEP sync pending",
                 (unsigned long)s_idle_s);
        break;

    case PC_PW_LIGHT:
        light_sleep_cycle(); /* 阻塞至循环退出 */
        break;

    case PC_PW_DEEP:
#if PC_PM_DEEP_SLEEP_ENABLE
        deep_sleep_enter(); /* 路径②:不返回 */
#else
        /* 路径①(默认):深睡阈值到达也不深睡,转入浅睡循环常驻
         * (理由见头文件:GPIO0 唤醒未验证,规格 §10/§15)。 */
        light_sleep_cycle();
#endif
        break;

    case PC_PW_ACTIVE:
    default:
        /* 升档只能经 activity();此处不可达,防御性忽略。 */
        break;
    }
}

bool pc_power_mgr_is_sleeping(void)
{
    return s_sleeping;
}

uint32_t pc_power_mgr_deep_wake_count(void)
{
    return (s_deep_magic == PC_PM_DEEP_MAGIC) ? s_deep_count : 0U;
}
