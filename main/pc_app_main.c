// main/pc_app_main.c —— PC Controller (presenter) build profile: 组装层。
//
// 本文件是 presenter 档的 app_main(),把纯逻辑模块(键位语义、应用
// 状态机、报告组帧、主机配置表、页码来源、演讲计时)与平台模块
// (BLE HOGP、NVS 存储、BSP、UI 桩)组装成完整应用。事实源:
//   - requirements.md §5 输入输出、§6 状态机、§7 并发模型
//     (行 157-164)、§8 持久化、§10 失败降级、§12 构建契约;
//   - ui-design.md(UI 接线点;正式页面由后续任务实现,本文件只
//     调用 pc_ui.h 约定接口)。
//
// 运行时不变量(规格 §7,逐条落实):
//   1. 按键回调只入队,不做任何重活(行 160);
//   2. 非 LVGL 上下文触碰任何 lv_*/pc_ui_* 前必须持
//      bsp_lvgl_lock()(本文件封装在 ui_lock/ui_unlock 中);
//   3. 一个应用任务消费统一事件队列,FSM/UI/HID/NVS 全在应用
//      任务内串行驱动(行 162);
//   4. 页面退出顺序:先停访问 UI 的定时器,再删屏——本档所有
//      页面切换都经 pc_ui_show_state(),退出顺序由 UI 层负责;
//      本层的反馈定时器在状态切换时先停止。
//
// 初始化降级链(参照 main.c 的 s_ok[] 模式,逐项说明见 app_main):
//   i2c -> storage -> display+LVGL(失败仅日志,无屏可运行,
//   规格 §10)-> button -> ble_hid -> 队列/任务/定时器。
//
// M3/M4 接线记录(原"未接入项",本里程碑已接;各点详见文内注释):
//   - 电源管理 (pc_power_mgr):背光 15 s 变暗 / 60 s 灭屏 / 浅睡 /
//     深睡降级路径(规格 §6/§10/FR-10);1 s 决策搭载 1 Hz 事件,
//     灭屏档的应用态惰性同步见 app_task;
//   - 按键音 (pc_beep):音频经 I2C 延迟初始化,失败静默降级,
//     开关 pp_cfg.key_sound 默认关(规格 §10/FR-09);
//   - 媒体音量本地展示计数(0..100,Consumer 协议无绝对音量回读,
//     FR-04)与菜单高亮 (pc_ui_set_menu_sel) 接线;
//   - 电量经 BAS 通知主机 (pc_ble_hid_battery_notify,FR-11,
//     见 handle_batt);
//   - 菜单项 3 (HOST PROFILE)/4 (KEY SOUND)/5 (BACKLIGHT) 的业
//     务变更 + 7 (ABOUT) 反馈页由 on_menu_confirm_apply 下发
//     (状态机吐完 SAVE_CFG 后,本层按 menu_sel 修改 s_cfg 并同步
//     全路端,见 on_menu_confirm_apply);
//   - 断连后重连广播链(规格 §10)在 handle_ble 的 DISCONNECTED
//     分支启动;重连定时器回调只入队(B3 修复:esp_timer 任务与
//     NimBLE host 任务间不互调 GAP API,实调迁到 app_task)。
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"

#include "pc_app_fsm.h"
#include "pc_beep.h"
#include "pc_ble_hid.h"
#include "pc_hid_reports.h"
#include "pc_host_profiles.h"
#include "pc_key_semantics.h"
#include "pc_power_mgr.h"
#include "pc_screenshot.h"
#include "pc_slide_counter.h"
#include "pc_speech_timer.h"
#include "pc_storage.h"
#include "pc_strings.h"
#include "pc_ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_app";

/* ---- 组装期常量 ---- */

#define APP_QUEUE_DEPTH 16      /* 统一事件队列深度(规格 §7) */
#define APP_TASK_STACK 4096     /* 单应用任务栈(规格 §7) */
#define TICK_PERIOD_US 1000000  /* 演讲计时 1 Hz(规格 §7 行 163) */
#define BATT_POLL_US 10000000   /* 电量轮询 10 s(规格 §7 行 163) */
#define FEEDBACK_MS 1500        /* 反馈页 1.5 s 自动返回(ui-design §4.4) */
#define RECON_DIRECTED_US 30000000 /* 断连重连:定向广播窗口 30 s(规格 §10) */
#define RECON_GENERAL_US 120000000 /* 随后通用广播窗口 2 min(规格 §10) */
#define FX_CAP 4                /* FSM 单次最大 3 个 effect,留 1 个余量 */
#define HID_KEY_BURST_GAP_MS 200 /* #57 跨平台全屏三连发间隔 */
#define BLE_RESET_ARM_WINDOW_MS 3000 /* #42 两步式重置二次确认窗口 */

/* 纯逻辑枚举与 BSP 枚举逐值同构校验(规格 §5:按键事件数值透传,
 * 无需转换)。任一侧改动都会在此编译期炸出,防止静默错位。
 * 注:GCC 升级后 -Werror=enum-compare 会拒绝跨匿名枚举比较,
 * 故显式转 int 再比较。 */
_Static_assert((int)PC_BTN_UP == (int)BSP_BTN_UP && (int)PC_BTN_DOWN == (int)BSP_BTN_DOWN &&
                   (int)PC_BTN_OK == (int)BSP_BTN_OK,
               "pc_btn_t / bsp_btn_t enumeration mismatch");
_Static_assert((int)PC_EV_PRESS == (int)BSP_BTN_PRESS && (int)PC_EV_CLICK == (int)BSP_BTN_CLICK &&
                   (int)PC_EV_DOUBLE == (int)BSP_BTN_DOUBLE && (int)PC_EV_LONG == (int)BSP_BTN_LONG,
               "pc_btn_ev_t / bsp_btn_ev_t enumeration mismatch");

/* ---- 统一事件队列(规格 §7 行 162) ---- */

typedef enum {
    PC_EQ_KEY = 0,       /* 按键事件(按键回调入队) */
    PC_EQ_BLE,           /* BLE 归一事件(NimBLE 回调入队) */
    PC_EQ_TICK,          /* 1 Hz(M3 起恒入队;演讲计时闸门移至处理侧,
                          * 电源 1 s 决策顺带搭载,规格 §6/§7) */
    PC_EQ_BATT,          /* 电量轮询 10 s */
    PC_EQ_FEEDBACK,      /* 反馈页 1.5 s 到时 */
    PC_EQ_RECON_DIR,     /* 断连重连:定向窗口 30 s 到时(B3 修复后
                          * 回调只入队,实调在 app_task) */
    PC_EQ_RECON_GEN,     /* 断连重连:通用窗口 2 min 到时(同上) */
} pc_evq_type_t;

typedef struct {
    pc_evq_type_t type;
    union {
        struct {
            uint8_t btn; /* bsp_btn_t 数值,与 pc_btn_t 同构 */
            uint8_t ev;  /* bsp_btn_ev_t 数值,与 pc_btn_ev_t 同构 */
        } key;
        struct {
            pc_ble_hid_evt_t ev;
            uint32_t arg; /* PASSKEY 时为 6 位配对码 */
        } ble;
    };
} pc_evq_t;

/* ---- 文件内状态 ---- */

static QueueHandle_t s_queue;
static TaskHandle_t s_app_task;

static pc_fsm_t s_fsm;               /* 应用状态机(应用任务独占) */
static pc_cfg_t s_cfg;               /* 全局配置缓存 */
static pc_speech_timer_t s_speech;   /* 演讲计时 */
static pc_page_source_t *s_page_src; /* 页码来源(本机估算器) */

static esp_timer_handle_t s_tick_timer;     /* 1 Hz 周期 */
static esp_timer_handle_t s_batt_timer;     /* 10 s 周期 */
static esp_timer_handle_t s_feedback_timer; /* 1.5 s 单次 */
static esp_timer_handle_t s_recon_dir_timer; /* 定向窗口 30 s 单次 */
static esp_timer_handle_t s_recon_gen_timer; /* 通用窗口 2 min 单次 */

/* 外设降级标志(参照 main.c s_ok[] 模式) */
static bool s_i2c_ok;
static bool s_storage_ok;
static bool s_display_ok;
static bool s_button_ok;
static bool s_ble_ok;
static bool s_battery_ok;

static volatile bool s_present_active; /* 1 Hz tick 入队闸门 */
static bool s_feedback_shown;          /* 反馈页在屏 */
static uint32_t s_queue_drops;         /* 队列满丢事件计数 */
static uint8_t s_volume = 50;          /* 媒体音量本地展示计数 0..99,
                                        * 初值取中。Consumer 协议只发相对
                                        * 步进(VOL_UP/DOWN),无绝对音量
                                        * 回读,本计数仅供屏显(规格 §1/
                                        * FR-04、ui-design §4.4"VOL nn")。
                                        * 上限 99 与 UI 路径钳制一致
                                        * (pc_ui_set_volume / pc_ui_media_set_volume)。 */

/* #42 两步式 BLE 重置武装状态:0 表示未武装;非零表示武装
 * 截止 tick(单位:FreeRTOS tick)。二次确认在窗口内即执行重置。 */
static TickType_t s_reset_arm_tick;

/* #42 重置武装超时定时器:3 s 到时后自动撤除武装(若未二次
 * 确认)。由 esp_timer 调度,仅重置标志,不显示反馈。 */
static esp_timer_handle_t s_reset_arm_timer;

static void app_task(void *arg);
static void run_effects(const pc_effect_t *fx, int n);
static void handle_recon_dir(void);
static void handle_recon_gen(void);
static void on_menu_confirm_apply(pc_action_t act);

/* ---- UI 助手:锁封装 + 降级 ----
 * 显示不可用(初始化失败/无屏降级,规格 §10)时静默跳过。 */

static bool ui_lock(void)
{
    if (!s_display_ok) return false;
    return bsp_lvgl_lock(200);
}

static void ui_unlock(void)
{
    bsp_lvgl_unlock();
}

static void ui_show_state(pc_state_t st)
{
    if (!ui_lock()) return;
    pc_ui_show_state(st);
    ui_unlock();
}

/* 反馈页:标题 + 副行;同时启动 1.5 s 自动返回定时器。 */
static void ui_feedback(const char *title, const char *detail)
{
    if (!ui_lock()) return;
    pc_ui_show_feedback(title, detail);
    ui_unlock();
    s_feedback_shown = true;
    esp_timer_start_once(s_feedback_timer, FEEDBACK_MS * 1000);
}

static void ui_set_link(bool connected, const char *host)
{
    if (!ui_lock()) return;
    pc_ui_set_link(connected, host);
    ui_unlock();
}

/* ---- 电源通知处理(组装层接线点) ----
 * 回调恒在应用任务上下文发出(见 pc_power_mgr.h 线程纪律),可直
 * 接驱动 FSM / BLE;本函数不触碰 LVGL——睡眠进入时屏幕已关,
 * 唤醒后的页面刷新由按键处理路径统一完成。 */
static void on_power_notify(pc_pm_ev_t ev)
{
    switch (ev) {
    case PC_PM_EV_LIGHT_ENTER: {
        /* 即将浅睡:应用态切 SLEEP(置唤醒吞键标志,规格 §6 转移表
         * 最后一行),并先停反馈定时器再离页(页面退出顺序,
         * 规格 §7 行 164)。 */
        if (s_feedback_shown) {
            esp_timer_stop(s_feedback_timer);
            s_feedback_shown = false;
        }
        if (s_fsm.state != PC_ST_SLEEP) {
            pc_effect_t fx[FX_CAP];
            int n = pc_fsm_on_power(&s_fsm, true, fx, FX_CAP);
            run_effects(fx, n);
        }
        break;
    }

    case PC_PM_EV_LIGHT_WINDOW:
        /* 浅睡周期性唤醒窗口(规格 §10 降级路径①:periodic
         * advertising windows):对当前槽开广播,让绑定主机有机会
         * 回连"唤醒"设备;窗口随下一次浅睡自然中断,无需显式停。 */
        if (s_ble_ok) {
            pc_slot_t s;
            pc_slot_load(s_cfg.slot, &s); /* 失败已回填默认值 */
            if (s.bound) {
                /* #54:传入持久化的 addr_type,定向广播用真实类型。 */
                (void)pc_ble_hid_start_adv_directed(s.addr, s.addr_type);
            } else {
                (void)pc_ble_hid_start_adv_general();
            }
        }
        break;

    case PC_PM_EV_DEEP_ENTER:
        /* 深睡前收尾(仅编译开关走路径②时到达):停无线。 */
        (void)pc_ble_hid_stop();
        break;

    case PC_PM_EV_OFF:
    case PC_PM_EV_WAKE:
    default:
        /* 灭屏档的 FSM 同步由 app_task 循环顶部惰性完成;唤醒后
         * 页面刷新由唤醒键的处理路径完成,此处无需动作。 */
        break;
    }
}

/* ---- 槽位元数据助手 ---- */

/* 取当前槽显示名(未绑定槽回退 "HOST<n>")。 */
static void slot_display_name(uint8_t slot, char out[17])
{
    pc_slot_t s;
    if (pc_slot_load(slot, &s) == ESP_OK && s.host_name[0] != '\0') {
        snprintf(out, 17, "%s", s.host_name);
    } else {
        snprintf(out, 17, "HOST%d", slot + 1);
    }
}

/* 配对成功:把对端身份地址、默认主机配置、锁屏组合键写进当前槽
 * (规格 §8 `pp_slot*` 布局),并把当前槽固化为配置槽。 */
static void save_slot_on_pair_ok(void)
{
    pc_slot_t s;
    pc_slot_load(s_fsm.slot, &s); /* 失败已回填默认值 */

    uint8_t addr[6];
    if (pc_ble_hid_peer_addr(addr) == ESP_OK) {
        memcpy(s.addr, addr, sizeof(s.addr));
        /* #54:同步保存 peer 地址类型。pc_ble_hid 模块内部
         * s_peer_addr_type 是最近一次连接 GAP 事件记录的
         * peer_id_addr.type,与 NimBLE 定向广播使用的类型一致;
         * 若未拿到对端地址(下面 else 分支)同样不要写 type,保持
         * 默认 BLE_ADDR_PUBLIC。 */
        s.addr_type = pc_ble_hid_peer_addr_type();
        s.bound = true;
    } else {
        ESP_LOGW(TAG, "pair ok but peer addr unknown; slot not marked bound");
    }

    s.os = s_cfg.default_os;
    const pc_combo_t *c = pc_lock_combo((pc_os_t)s.os);
    if (c != NULL) {
        /* 槽位档案记录锁屏组合键;锁屏时优先用档案,缺失才回退
         * 主机配置表(见 SHOW_FEEDBACK_LOCK 处理) */
        s.lock_mods = c->mods;
        s.lock_key = c->keycode;
    }
    slot_display_name(s_fsm.slot, s.host_name);
    s.last_use = (uint32_t)(esp_timer_get_time() / 1000000);

    if (pc_slot_save(s_fsm.slot, &s) != ESP_OK) {
        ESP_LOGW(TAG, "slot %u metadata save failed (degraded)", (unsigned)s_fsm.slot);
    }
    s_cfg.slot = s_fsm.slot;
    if (pc_cfg_save(&s_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "cfg save failed (degraded)");
    }
}

/* ---- 回调:按键 / BLE / 定时器(一律只入队) ---- */

/* 按键回调运行在 button 组件的定时器任务(规格 §5/§7 行 160):
 * 只入队;队列满时丢最老事件并计数记日志(按键语义是"最新意图
 * 优先",丢老事件损失最小)。 */
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_KEY;
    e.key.btn = (uint8_t)btn;
    e.key.ev = (uint8_t)ev;
    if (xQueueSend(s_queue, &e, 0) != pdTRUE) {
        pc_evq_t discard;
        xQueueReceive(s_queue, &discard, 0); /* 丢最老 */
        (void)xQueueSend(s_queue, &e, 0);
        s_queue_drops++;
        ESP_LOGW(TAG, "queue full, dropped oldest (%lu total)",
                 (unsigned long)s_queue_drops);
    }
}

/* BLE 事件回调运行在 NimBLE host 任务(规格 §7 行 161):只入队。 */
static void on_ble_evt(pc_ble_hid_evt_t ev, uint32_t arg, void *user)
{
    (void)user;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_BLE;
    e.ble.ev = ev;
    e.ble.arg = arg;
    if (xQueueSend(s_queue, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, BLE evt %d dropped", (int)ev);
    }
}

/* esp_timer 回调运行在定时器任务:只入队。
 * M3 接线:1 Hz 事件恒入队——原"仅演示且已连接"的演讲计时闸门
 * 移至处理侧 (handle_tick),使电源 1 s 决策 (pc_power_mgr_tick_1s)
 * 能顺带搭载同一事件,不新增常驻定时器(规格 §6/§7)。 */
static void on_tick(void *arg)
{
    (void)arg;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_TICK;
    (void)xQueueSend(s_queue, &e, 0);
}

static void on_batt_poll(void *arg)
{
    (void)arg;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_BATT;
    (void)xQueueSend(s_queue, &e, 0);
}

static void on_feedback_timeout(void *arg)
{
    (void)arg;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_FEEDBACK;
    (void)xQueueSend(s_queue, &e, 0);
}

/* 规格 §10 重连链:定向 30 s 窗口到时 -> 切通用广播 2 min。
 * 回调只入队(B3 修复:esp_timer 任务不得与 NimBLE host 任务
 * 竞争 GAP API,实调推到 app_task,避免组件间共享资源竟用)。 */
static void on_recon_directed_expired(void *arg)
{
    (void)arg;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_RECON_DIR;
    (void)xQueueSend(s_queue, &e, 0);
}

/* 通用广播 2 min 窗口到时:停止广播,进入静默待机(任意按键后
 * pc_power_mgr_activity() 重置空闲计时并还原背光;重开广播
 * 由按键动作链驱动,见 handle_key)。回调同样只入队(B3)。 */
static void on_recon_general_expired(void *arg)
{
    (void)arg;
    pc_evq_t e = { 0 };
    e.type = PC_EQ_RECON_GEN;
    (void)xQueueSend(s_queue, &e, 0);
}

/* ---- effect 执行 ---- */

/* 发送锁屏组合键 + 屏显反馈(组装层链路:
 * STANDBY OK LONG -> FSM(PC_FX_SHOW_FEEDBACK_LOCK)-> 本函数)。
 * 设计说明:FSM 只吐"显示锁屏反馈"的意图,组合键本体由本层按
 * 当前槽位档案组装(槽内记录的组合键优先,缺失回退主机配置表
 * pc_lock_combo)——FSM 保持零平台依赖、槽位数据属于平台层。 */
static void do_lock_combo(void)
{
    pc_slot_t s;
    pc_slot_load(s_fsm.slot, &s); /* 失败已回填默认值 */

    uint8_t mods = s.lock_mods;
    uint8_t key = s.lock_key;
    if (mods == 0 || key == 0) {
        const pc_combo_t *c = pc_lock_combo((pc_os_t)s.os);
        if (c != NULL) {
            mods = c->mods;
            key = c->keycode;
        }
    }
    if (mods == 0 || key == 0) {
        /* 未知主机配置:宁可不锁也不发错组合(规格 §1/FR-03) */
        ESP_LOGW(TAG, "lock: unknown profile for slot %u, skip send",
                 (unsigned)s_fsm.slot);
        return;
    }

    pc_kbd_report_t r;
    pc_kbd_clear(&r);
    if (!pc_kbd_add(&r, mods, key)) {
        ESP_LOGE(TAG, "lock: combo build failed");
        return;
    }
    esp_err_t err = pc_ble_hid_send_keyboard(&r); /* 内部必跟空报告释放 */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lock send failed: %s (degraded)", esp_err_to_name(err));
    }

    /* 屏显反馈:主词 + 组合键/主机配置副行(ui-design §4.4) */
    const char *combo = pc_combo_text((pc_os_t)s.os);
    const char *profile = pc_profile_name((pc_os_t)s.os);
    char detail[48];
    snprintf(detail, sizeof(detail), "%s / PROFILE: %s",
             combo ? combo : "?", profile ? profile : "?");
    ui_feedback(pc_str_en[PC_STR_FB_LOCKED], detail);
}

static void run_effects(const pc_effect_t *fx, int n)
{
    /* #57:跨平台全屏三连发——连续两帧 HID_KEY effect 之间需 200 ms
     * 间隔,避免主机侧把多组合键视为同一次快速连击(尤其 macOS
     * PowerPoint / Keynote)。仅在该函数本次调用内连续出现 HID_KEY
     * 时插 delay;其它 effect 序列(翻页/锁屏等)不会被误伤。 */
    bool last_was_hid_key = false;
    for (int i = 0; i < n; i++) {
        if (i > 0 && fx[i].type == PC_FX_HID_KEY && last_was_hid_key) {
            vTaskDelay(pdMS_TO_TICKS(HID_KEY_BURST_GAP_MS));
        }
        last_was_hid_key = (fx[i].type == PC_FX_HID_KEY);
        switch (fx[i].type) {
        case PC_FX_HID_KEY: {
            /* 普通键:组帧 -> 发送(内部补空报告释放,规格 §1/FR-01) */
            pc_kbd_report_t r;
            pc_kbd_clear(&r);
            if (pc_kbd_add(&r, fx[i].arg.key.mods, fx[i].arg.key.keycode)) {
                esp_err_t err = pc_ble_hid_send_keyboard(&r);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "kbd send failed: %s", esp_err_to_name(err));
                }
            }
            break;
        }

        case PC_FX_HID_CONSUMER: {
            /* press + release 两帧在发送接口内部完成 */
            uint16_t usage = fx[i].arg.usage;
            (void)pc_ble_hid_send_consumer(usage);
            /* 媒体音量本地展示计数接线(规格 §1/FR-04):Consumer
             * 协议只发相对步进,无绝对音量回读,读数由本层维护,
             * 发送失败不回滚(展示性计数,无真值可对)。经
             * pc_ui_set_volume 推给媒体页(持锁;仅媒体页在屏时
             * 生效,越界钳制,见 pc_ui.h)。线程上下文:应用任务。 */
            if (usage == PC_USAGE_VOL_UP && s_volume < 99U) {
                s_volume++;
            } else if (usage == PC_USAGE_VOL_DOWN && s_volume > 0U) {
                s_volume--;
            }
            if (usage == PC_USAGE_VOL_UP || usage == PC_USAGE_VOL_DOWN) {
                if (ui_lock()) {
                    pc_ui_set_volume((int)s_volume);
                    ui_unlock();
                }
            }
            break;
        }

        case PC_FX_PAGE_ENTER_FULLSCREEN:
            /* FR-12:进全屏页码复位为 1 */
            pc_local_fullscreen_entered(s_page_src);
            if (ui_lock()) {
                pc_ui_set_slide(pc_page_get(s_page_src));
                ui_unlock();
            }
            break;

        case PC_FX_PAGE_EXIT_FULLSCREEN:
            pc_local_fullscreen_exited(s_page_src);
            if (ui_lock()) {
                pc_ui_set_slide(pc_page_get(s_page_src)); /* -1 -> 不画 */
                ui_unlock();
            }
            break;

        case PC_FX_PAGE_STEP:
            pc_local_step(s_page_src, fx[i].arg.page_delta);
            if (ui_lock()) {
                pc_ui_set_slide(pc_page_get(s_page_src));
                ui_unlock();
            }
            break;

        case PC_FX_TIMER_RESET:
            /* FR-05:进入演示模式计时清零 + 解除暂停 (任务 #47)。 */
            pc_speech_reset(&s_speech);
            if (ui_lock()) {
                pc_ui_set_timer("00:00");
                /* 复位 UI 计时状态词为 RUN; 若退演示后再进入,
                 * 暂停状态已被 pc_speech_reset 清掉, UI 同步恢复。 */
                pc_ui_present_set_paused(false);
                ui_unlock();
            }
            break;

        case PC_FX_START_PAIR: {
            /* 已绑定槽重配对 -> 定向广播;空槽 -> 通用广播(配对码
             * 在主机发起配对时经 PASSKEY 事件屏显,规格 §1/FR-07)。
             * #54:定向广播用槽位持久化的 addr_type。 */
            pc_slot_t s;
            pc_slot_load(s_fsm.slot, &s);
            if (s.bound) {
                (void)pc_ble_hid_start_adv_directed(s.addr, s.addr_type);
            } else {
                (void)pc_ble_hid_start_adv_general();
            }
            break;
        }

        case PC_FX_STOP_PAIR:
            (void)pc_ble_hid_stop_adv();
            break;

        case PC_FX_SLOT_SWITCH: {
            /* FR-06:先优雅断链(断开后按 HOGP 进 suspend 态),
             * 再对目标槽定向广播;目标槽未绑定则通用广播。
             * C2 修复:若当前状态是配对页(PC_ST_PAIR),
             * 同步推一行刷新 "SLOT n/3",避免切槽后屏显滞后。 */
            uint8_t target = fx[i].arg.slot;
            (void)pc_ble_hid_graceful_disconnect();
            s_cfg.slot = target;
            (void)pc_cfg_save(&s_cfg);
            /* 同步电源管理器的槽位上下文(深睡 RTC 保留区诊断用,
             * 见 pc_power_mgr.h;仅路径②有意义,路径①无副作用)。 */
            pc_power_mgr_set_slot_ctx(target);
            pc_slot_t s;
            pc_slot_load(target, &s);
            if (s.bound) {
                /* #54:定向广播用槽位持久化的 addr_type。 */
                (void)pc_ble_hid_start_adv_directed(s.addr, s.addr_type);
            } else {
                (void)pc_ble_hid_start_adv_general();
            }
            if (s_fsm.state == PC_ST_PAIR && ui_lock()) {
                pc_ui_set_pair_slot((int)target + 1);
                ui_unlock();
            }
            break;
        }

        case PC_FX_SLOT_CLEAR:
            /* FR-08:只删槽命名空间键,不擦分区 */
            (void)pc_slot_clear(fx[i].arg.slot);
            ui_feedback(pc_str_en[PC_STR_FB_SLOT_CLEARED], "");
            break;

        case PC_FX_SAVE_CFG:
            (void)pc_cfg_save(&s_cfg);
            ui_feedback(pc_str_en[PC_STR_FB_SAVED], "");
            break;

        case PC_FX_SHOW_FEEDBACK_LOCK:
            do_lock_combo();
            break;

        case PC_FX_DISCONNECT:
            (void)pc_ble_hid_graceful_disconnect();
            break;

        case PC_FX_ADV_RECONNECT: {
            /* 规格 §10:定向 30 s -> 通用 2 min(时长由
             * s_recon_dir_timer / s_recon_gen_timer 控制)。
             * #54:定向广播用真实地址类型。 */
            pc_slot_t s;
            pc_slot_load(s_cfg.slot, &s);
            esp_timer_stop(s_recon_gen_timer); /* 防旧窗口残留 */
            if (s.bound &&
                pc_ble_hid_start_adv_directed(s.addr, s.addr_type) == ESP_OK) {
                esp_timer_start_once(s_recon_dir_timer, RECON_DIRECTED_US);
            } else {
                (void)pc_ble_hid_start_adv_general();
                esp_timer_start_once(s_recon_gen_timer, RECON_GENERAL_US);
            }
            break;
        }

        case PC_FX_ENTER_MEDIA:
            /* 进入媒体模式:把本地音量计数推给媒体页,保证页内读数
             * 与组装层一致(无绝对音量回读,见 FR-04 注释)。
             * 按键音已在 handle_key 分发点统一播放,不在此重复。 */
            if (ui_lock()) {
                pc_ui_set_volume((int)s_volume);
                ui_unlock();
            }
            break;

        case PC_FX_TIMER_TOGGLE:
            /* 任务 #47: 切换演讲计时器暂停/恢复。
             *   1. 翻转 pc_speech_timer_t.paused;
             *   2. 把 "PAUSED" / "RUN" 状态词推给演示页 (UI 局部
             *      脏区, 1-4 KB/s 不超预算);
             *   3. 不发任何 HID 帧 (本动作纯展示);
             *   4. 不响按键音 (规格 §1/FR-09: 仅用户语义动作响音;
             *      暂停切换属 UI 反馈而非应用语义)。
             * 线程上下文: 应用任务。 */
            pc_speech_set_paused(&s_speech,
                                 !pc_speech_is_paused(&s_speech));
            if (ui_lock()) {
                pc_ui_present_set_paused(pc_speech_is_paused(&s_speech));
                ui_unlock();
            }
            break;

        default:
            ESP_LOGW(TAG, "unknown effect %d", (int)fx[i].type);
            break;
        }
    }
}

/* ---- 事件处理(应用任务) ---- */

static void handle_key(const pc_evq_t *e)
{
    /* 电源活动上报(规格 §6 电源子状态机、FR-10):任意有效用户
     * 事件先重置空闲计时并还原背光(内部按需);"任意按键立即提升
     * 广播占空比"(规格 §10)由断连重连广播链驱动,见 PC_FX_
     * ADV_RECONNECT 处理。
     * 深睡唤醒说明:深睡唤醒 = 芯片冷启动,app_main 重跑,无"首键"
     * 问题;"首键只唤醒不执行功能"覆盖灭屏/浅睡档位的唤醒,由既有
     * wake_key_pending 机制承担——电源进入睡眠档时应用态被切到
     * PC_ST_SLEEP(置吞键标志,见 app_task 惰性同步),唤醒首键经按键语义层映射为 PC_ACT_WAKE 被吞(规格 §6 转移表最后一行)。
     * 线程上下文:应用任务。 */
    pc_power_mgr_activity();

    pc_action_t act = pc_key_map(s_fsm.state, (pc_btn_t)e->key.btn,
                                 (pc_btn_ev_t)e->key.ev,
                                 pc_ble_hid_connected());

    pc_state_t prev = s_fsm.state;
    pc_effect_t fx[FX_CAP];
    int n = pc_fsm_on_action(&s_fsm, act, pc_ble_hid_connected(), fx, FX_CAP);
    /* 菜单确认业务下发(A1/A3 修复):在 run_effects 之前调用,
     * 使业务变更与 SAVE_CFG 同一原子事务写入;PC_ACT_MENU_CONFIRM
     * 下状态机吐完 SAVE_CFG 后,本层调 on_menu_confirm_apply 按
     * s_fsm.menu_sel 修改 s_cfg(OS/背光/按键音/ABOUT 反馈),
     * 再由 run_effects 解释 SAVE_CFG 把修改后的 s_cfg 写 NVS。 */
    on_menu_confirm_apply(act);
    run_effects(fx, n);

    /* 菜单高亮接线:MENU_OPEN / MENU_NEXT / MENU_PREV 后把 FSM 的
     * 选中项推给菜单页(持锁;仅菜单页在屏时生效,仅局部重绘新旧两行,
     * 见 pc_ui.h/ui-design §4.1)。线程上下文:应用任务。 */
    if (act == PC_ACT_MENU_OPEN || act == PC_ACT_MENU_NEXT ||
        act == PC_ACT_MENU_PREV) {
        if (ui_lock()) {
            pc_ui_set_menu_sel((int)s_fsm.menu_sel);
            ui_unlock();
        }
    }

    /* 按键音(FR-09):有效动作分发点播放;开关在 pc_beep 内判断
     * (组装层随配置同步);SLEEP 态唤醒首键只唤醒不播(规格 §6),
     * 被拒绝按键按 ui-design §7 播低音。任何音频失败静默降级,
     * 不影响按键功能路径(规格 §10)。线程上下文:应用任务。 */
    if (prev != PC_ST_SLEEP && act != PC_ACT_WAKE) {
        if (act == PC_ACT_NONE) {
            pc_beep_play(PC_BEEP_ERROR);
        } else {
            pc_beep_kind_t k =
                (act == PC_ACT_PAGE_NEXT || act == PC_ACT_PAGE_PREV ||
                 act == PC_ACT_VOL_UP || act == PC_ACT_VOL_DOWN)
                    ? PC_BEEP_PAGE
                    : PC_BEEP_CONFIRM;
            pc_beep_play(k);
        }
    }

    /* 状态切换:先停反馈定时器(页面退出顺序,规格 §7 行 164),
     * 再刷新页面。 */
    if (s_fsm.state != prev) {
        if (s_feedback_shown) {
            esp_timer_stop(s_feedback_timer);
            s_feedback_shown = false;
        }
        ui_show_state(s_fsm.state);
    }
}

static void handle_ble(const pc_evq_t *e)
{
    pc_state_t prev = s_fsm.state;
    pc_effect_t fx[FX_CAP];
    int n;

    switch (e->ble.ev) {
    case PC_BLE_EVT_CONNECTED: {
        /* 连接建立属有效用户事件:重置电源空闲计时(规格 §6)。 */
        pc_power_mgr_activity();
        /* B4 修复:连接建立后取消重连广播链(定向/通用窗口均停),
         * 避免重复广播与主机侧重连逻辑冲突——重连链是断开后的
         * 抢占动作,连接一旦建立就交给主机驱动。 */
        esp_timer_stop(s_recon_dir_timer);
        esp_timer_stop(s_recon_gen_timer);
        n = pc_fsm_on_ble(&s_fsm, PC_BLE_CONNECTED, fx, FX_CAP);
        run_effects(fx, n);
        /* 更新槽位最近使用时间(规格 §8) */
        pc_slot_t s;
        if (pc_slot_load(s_cfg.slot, &s) == ESP_OK && s.bound) {
            s.last_use = (uint32_t)(esp_timer_get_time() / 1000000);
            (void)pc_slot_save(s_cfg.slot, &s);
        }
        char name[17];
        slot_display_name(s_cfg.slot, name);
        ui_set_link(true, name);
        break;
    }

    case PC_BLE_EVT_DISCONNECTED: {
        n = pc_fsm_on_ble(&s_fsm, PC_BLE_DISCONNECTED, fx, FX_CAP);
        run_effects(fx, n); /* FSM 在演示/媒体态断连时回待机,
                             * 并吐 PAGE_EXIT_FULLSCREEN 等 effect */
        ui_set_link(false, "");
        /* A2 修复:断连后启动重连广播链(规格 §10)
         *   定向 30 s -> 通用 2 min -> 停。
         * 由组装层发起,不依赖 FSM 吐 PC_FX_ADV_RECONNECT(状态机
         * 已在该事件中吐 PAGE_EXIT_FULLSCREEN,与广播控制重复会
         * 二次启定时器)。 */
        if (s_ble_ok) {
            pc_slot_t sl;
            pc_slot_load(s_cfg.slot, &sl); /* 失败已回填默认值 */
            esp_timer_stop(s_recon_gen_timer); /* 防旧窗口残留 */
            if (sl.bound &&
                pc_ble_hid_start_adv_directed(sl.addr, sl.addr_type) == ESP_OK) {
                esp_timer_start_once(s_recon_dir_timer,
                                     RECON_DIRECTED_US);
            } else {
                (void)pc_ble_hid_start_adv_general();
                esp_timer_start_once(s_recon_gen_timer,
                                     RECON_GENERAL_US);
            }
        }
        break;
    }

    case PC_BLE_EVT_PAIR_OK:
        pc_power_mgr_activity(); /* 配对成功同属有效用户事件 */
        save_slot_on_pair_ok();
        (void)pc_ble_hid_stop_adv(); /* 绑定完成,停止配对广播 */
        /* B4:配对成功同样意味着链路恢复,取消可能残留的重连窗口
         * ——虽然断连->重连->连接的时间序列里通常窗口已自然到期,
         * 但配置下重连链可能仍在跑(例如配对覆盖),显式停一次更稳。 */
        esp_timer_stop(s_recon_dir_timer);
        esp_timer_stop(s_recon_gen_timer);
        n = pc_fsm_on_ble(&s_fsm, PC_BLE_PAIR_OK, fx, FX_CAP);
        run_effects(fx, n);
        /* B2 修复:先调 ui_show_state 把 PAIR -> STANDBY 的状态页
         * 切到待机,再调 ui_feedback 覆盖为配对成功反馈(1.5 s 后
         * 自动返回)。原顺序会被 handle_ble 末尾的 ui_show_state
         * 覆盖,反馈页闪现即逝。 */
        if (s_fsm.state != prev) {
            if (s_feedback_shown) {
                esp_timer_stop(s_feedback_timer);
                s_feedback_shown = false;
            }
            ui_show_state(s_fsm.state);
        }
        ui_feedback(pc_str_en[PC_STR_FB_PAIR_OK], "");
        /* 显式返回,避免 handle_ble 末尾的"状态变化 -> 刷新"
         * 块重复跑(B2 修复:已在本分支处理)。 */
        return;

    case PC_BLE_EVT_PAIR_FAIL:
        n = pc_fsm_on_ble(&s_fsm, PC_BLE_PAIR_FAIL, fx, FX_CAP);
        run_effects(fx, n);
        ui_feedback(pc_str_en[PC_STR_FB_PAIR_FAIL], "");
        break;

    case PC_BLE_EVT_PASSKEY:
        /* 6 位配对码屏显(规格 §1/FR-07) */
        if (ui_lock()) {
            pc_ui_show_passkey(e->ble.arg);
            ui_unlock();
        }
        break;

    default:
        break;
    }

    if (s_fsm.state != prev) {
        if (s_feedback_shown) {
            esp_timer_stop(s_feedback_timer);
            s_feedback_shown = false;
        }
        ui_show_state(s_fsm.state);
    }
}

static void handle_tick(void)
{
    /* 演讲计时:维持原闸门语义(仅演示模式且已连接,规格 §7;
     * 闸门从入队侧移到本处理函数,使 1 Hz 事件可顺带驱动电源)。 */
    if (s_present_active) {
        pc_speech_tick(&s_speech);
        char buf[9]; /* 任务 #47: 缓冲 9 字节以容纳 "HH:MM:SS"。 */
        pc_speech_format(&s_speech, buf);
        if (ui_lock()) {
            pc_ui_set_timer(buf);
            ui_unlock();
        }
    }
    /* 电源 1 s 决策 + 执行(规格 §6 电源子状态机):内部自判重入(浅睡循环中直接返回);到达浅睡档时内部阻塞(循环),故仅在应用任务上下文调用。线程上下文:应用任务。 */
    pc_power_mgr_tick_1s();
}

static void handle_batt(void)
{
    int soc = bsp_battery_soc(); /* I2C 失败时返回 -1 */
    if (ui_lock()) {
        pc_ui_set_battery(soc); /* -1 -> 只画图标轮廓(规格 §10) */
        ui_unlock();
    }
    /* 读数 -1 时停止通知(规格 §10);连接中才通知 */
    if (soc >= 0 && pc_ble_hid_connected()) {
        (void)pc_ble_hid_battery_notify((uint8_t)soc);
    }
}

static void handle_feedback_timeout(void)
{
    if (!s_feedback_shown) return;
    s_feedback_shown = false;
    ui_show_state(s_fsm.state); /* 回到当前模式页 */
}

/* ---- #42 两步式 BLE 重置助手 ---- */

/* 重置武装超时回调:3 s 窗口自然过期。仅重置标志,不显示反馈
 * (用户已离开该菜单项或放弃二次确认均视为取消)。运行于 esp_timer
 * 任务,只更新静态变量,线程安全。 */
static void on_reset_arm_expired(void *arg)
{
    (void)arg;
    s_reset_arm_tick = 0;
    ESP_LOGI(TAG, "BLE reset: arm window expired, disarmed silently");
}

/* 执行重置:nvs_flash_erase + esp_restart。注意该函数不返回。 */
static void do_ble_reset(void)
{
    ESP_LOGW(TAG, "BLE reset: user-confirmed, erasing NVS and restarting");
    /* 先显示一条极短的反馈——esp_restart 不会等反馈完成,但 UI
     * 已经在调用前由 ui_feedback 推到屏,屏显残影足以让用户看到
     * “正在重置”。真正重启在 nvs_flash_erase 完成后立刻发生。 */
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE reset: nvs_flash_erase failed: %s",
                 esp_err_to_name(err));
        /* 即使擦除失败也重启,让 bootloader 走默认恢复路径 */
    }
    esp_restart();
}

static void do_ble_reset_arm(void)
{
    /* 武装:记录当前 tick + 截止时间;启 3 s 一次定时器,到时撤
     * 除武装。同一菜单项二次确认进 do_ble_reset_confirm。 */
    TickType_t now = xTaskGetTickCount();
    s_reset_arm_tick = now + pdMS_TO_TICKS(BLE_RESET_ARM_WINDOW_MS);
    esp_timer_stop(s_reset_arm_timer); /* 防旧计时残留 */
    esp_timer_start_once(s_reset_arm_timer,
                         (uint64_t)BLE_RESET_ARM_WINDOW_MS * 1000U);
    ESP_LOGW(TAG, "BLE reset: armed, awaiting second confirm within %u ms",
             (unsigned)BLE_RESET_ARM_WINDOW_MS);
}

static bool do_ble_reset_confirm(void)
{
    /* 二次确认:仅在窗口内返回 true(调用方随即重置)。 */
    if (s_reset_arm_tick == 0) return false;
    TickType_t now = xTaskGetTickCount();
    if (now < s_reset_arm_tick) {
        s_reset_arm_tick = 0;
        esp_timer_stop(s_reset_arm_timer);
        return true;
    }
    /* 窗口已过期:视为新一次的首次确认,重新武装。 */
    do_ble_reset_arm();
    return false;
}

/* ---- 菜单确认业务下发(A1 修复) ----
 * 状态机吐完 PC_FX_SAVE_CFG 后,组装层根据 s_fsm.menu_sel 修改
 * s_cfg + 同步全路端:背光(电源管理器)/按键音(蜂鸣器)/
 * 主机配置(锁屏组合键同步到当前槽位档案)。这样状态机保持
 * 零平台依赖(不持 s_cfg),业务语义集中在本层。
 * 菜单项仅在 PC_ACT_MENU_CONFIRM 且原状态为 MENU 时下发;
 * PC_FX_SAVE_CFG 在 run_effects 中负责写 NVS。 */
static void on_menu_confirm_apply(pc_action_t act)
{
    if (act != PC_ACT_MENU_CONFIRM) return;
    if (s_fsm.state != PC_ST_STANDBY_MENU) return;

    switch (s_fsm.menu_sel) {
    case 3: /* HOST PROFILE:循环 OS + 同步当前槽锁屏组合键。
             * s_cfg.default_os 持久化为"新配对槽的默认档案"
             * (规格 §8);同时更新当前已绑定槽的锁屏组合键,避免
             * 切槽/重启后锁屏仍使用旧档案的组合键。 */
        s_cfg.default_os = (uint8_t)((s_cfg.default_os + 1U) %
                                      (uint32_t)PC_OS_MAX);
        {
            pc_slot_t sl;
            (void)pc_slot_load(s_fsm.slot, &sl);
            if (sl.bound) {
                sl.os = s_cfg.default_os;
                const pc_combo_t *c =
                    pc_lock_combo((pc_os_t)sl.os);
                if (c != NULL) {
                    sl.lock_mods = c->mods;
                    sl.lock_key = c->keycode;
                }
                (void)pc_slot_save(s_fsm.slot, &sl);
            }
        }
        break;

    case 4: /* KEY SOUND:翻转开关 + 同步蜂鸣器(规格 §1/FR-09)。 */
        s_cfg.key_sound = !s_cfg.key_sound;
        pc_beep_set_enabled(s_cfg.key_sound);
        break;

    case 5: /* BACKLIGHT:循环档 100 -> 50 -> 20 -> 100(任务订点)。
             * 同步立即调背光(当前可见效果),并更新电源管理器
             * ACTIVE 档位(唤醒/还原源)。 */
        s_cfg.backlight = (s_cfg.backlight >= 100U) ? 50U :
                          (s_cfg.backlight >= 50U)  ? 20U : 100U;
        bsp_display_backlight(s_cfg.backlight);
        pc_power_mgr_set_active_backlight(s_cfg.backlight);
        break;

    case 7: /* ABOUT:仅屏显,不修改任何配置(纯文案反馈页,
             * A3 修复)。菜单选中项在反馈定时器到时后状态机
             * 不动,UI 反馈页返回原菜单页。 */
        ui_feedback(pc_str_en[PC_STR_ABOUT_APP],
                    pc_str_en[PC_STR_ABOUT_VERSION]);
        break;

    case 8: /* #42 RESET BLE:两步式确认。
             * 状态机对项 8 不吐 effect,业务全部在本函数完成:
             *   - 首次确认(未武装或窗口已过期):武装,反馈 ARMED;
             *   - 窗口内二次确认:立即反馈 RESETTING 并调用 do_ble_reset
             *     (内部 nvs_flash_erase + esp_restart,不会返回)。
             * 重置路径必须从已武装状态触发,避免误触。 */
        if (do_ble_reset_confirm()) {
            ui_feedback(pc_str_en[PC_STR_FB_RESET_CONFIRM], "");
            do_ble_reset(); /* 不会返回 */
        } else {
            /* 首次确认或窗口已自动撤除后的二次确认:重新武装并反馈 */
            do_ble_reset_arm();
            ui_feedback(pc_str_en[PC_STR_FB_RESET_ARM], "");
        }
        break;

    case 9: /* #46 SCREENSHOT:触发一次串口截屏。
             * 状态机对项 9 不吐 effect(纯展示触发),业务在本函数:
             *   - pc_screenshot_capture() 内部按 LVGL 锁抓取并按
             *     8 行分块通过 USB Serial/JTAG 推出去,失败仅日志;
             *   - 给一个 1.5 s 反馈页提示用户"截屏已发出";
             *   - 截屏是异步于 USB 传输的(分块写出期间 UI 仍可响
             *     应),反馈页定时器与 USB 写出重叠不影响。
             * 线程上下文:应用任务(非 LVGL);截图内部自行持锁。 */
        pc_screenshot_capture();
        ui_feedback(pc_str_en[PC_STR_MENU_SCREENSHOT], "");
        break;

    default:
        /* 其它菜单项(0/1/2/6)业务在 FSM 或 effect 层处理。 */
        break;
    }
}

/* ---- 断连重连广播链路(B3:回调只入队,实调在应用任务) ---- */

/* 定向 30 s 到时:切到通用广播 2 min。 */
static void handle_recon_dir(void)
{
    (void)pc_ble_hid_start_adv_general();
    esp_timer_start_once(s_recon_gen_timer, RECON_GENERAL_US);
}

/* 通用 2 min 到时:停广播,静默待机。 */
static void handle_recon_gen(void)
{
    (void)pc_ble_hid_stop_adv();
}

static void app_task(void *arg)
{
    (void)arg;
    pc_evq_t e;
    for (;;) {
        if (xQueueReceive(s_queue, &e, portMAX_DELAY) != pdTRUE) continue;

        /* 电源睡眠惰性同步(规格 §6 转移表"Any active mode |
         * Power timeout | SLEEP"):灭屏档到达时应用态未即时切换,
         * 在任何待处理事件之前补切——置 wake_key_pending,保证
         * 唤醒首键只唤醒不执行功能(首键吞掉)。线程上下文:
         * 应用任务;灭屏期间屏幕已关,此处不刷页。 */
        if (pc_power_mgr_is_sleeping() && s_fsm.state != PC_ST_SLEEP) {
            if (s_feedback_shown) {
                esp_timer_stop(s_feedback_timer);
                s_feedback_shown = false;
            }
            pc_effect_t sfx[FX_CAP];
            int sn = pc_fsm_on_power(&s_fsm, true, sfx, FX_CAP);
            run_effects(sfx, sn);
        }

        switch (e.type) {
        case PC_EQ_KEY:         handle_key(&e); break;
        case PC_EQ_BLE:         handle_ble(&e); break;
        case PC_EQ_TICK:        handle_tick(); break;
        case PC_EQ_BATT:        handle_batt(); break;
        case PC_EQ_FEEDBACK:    handle_feedback_timeout(); break;
        case PC_EQ_RECON_DIR:   handle_recon_dir(); break;
        case PC_EQ_RECON_GEN:   handle_recon_gen(); break;
        default: break;
        }
        /* 演讲计时闸门随状态刷新(1 Hz 回调读) */
        s_present_active =
            (s_fsm.state == PC_ST_PRESENT) && pc_ble_hid_connected();
    }
}

/* ---- 启动 ---- */

void app_main(void)
{
    ESP_LOGI(TAG, "PC Controller (presenter) profile: assembly layer boot");

    /* 1. I2C 总线:电量计/音频共用(失败 -> 电量与按键音降级,
     *    规格 §10;不阻塞启动)。 */
    s_i2c_ok = (bsp_i2c_init() == ESP_OK);
    if (!s_i2c_ok) ESP_LOGE(TAG, "i2c init failed; battery/audio degraded");

    /* 电量计初始化失败:轮询恒返回 -1,走"电量不可用"降级路径。 */
    s_battery_ok = s_i2c_ok && (bsp_battery_init() == ESP_OK);
    if (!s_battery_ok) ESP_LOGW(TAG, "battery unavailable; UI degrades");

    /* 2. 存储:失败不擦除,全部读取降级为内存默认值(规格 §10)。 */
    s_storage_ok = (pc_storage_init() == ESP_OK);
    if (!s_storage_ok) {
        ESP_LOGE(TAG, "storage init failed; running on in-memory defaults");
    }
    (void)pc_cfg_load(&s_cfg); /* 失败已回填默认值 */

    /* 2.5 按键音模块(规格 §1/FR-09):仅状态初始化,音频硬件延迟到
     *    首次播放(规格 §10 静默降级,见 pc_beep.h);开关跟随配置项
     *    `pp_cfg.key_sound`,默认关。线程上下文:启动阶段单线程。 */
    pc_beep_init();
    pc_beep_set_enabled(s_cfg.key_sound);

    /* 3. 显示 + LVGL:失败仅日志,无屏可运行——按键与无线功能
     *    不依赖屏幕(规格 §10"显示初始化失败:启动继续")。 */
    if (bsp_display_init() == ESP_OK && bsp_lvgl_init() != NULL) {
        s_display_ok = true;
        if (ui_lock()) {
            pc_ui_init();
            ui_unlock();
        }
        bsp_display_backlight(s_cfg.backlight);
    } else {
        ESP_LOGE(TAG, "display/LVGL init failed; running headless");
    }

    /* 4. 统一事件队列 + 单应用任务(规格 §7 行 162)。必须先于任何
     *    会入队的回调(按键/蓝牙/定时器)存在,否则回调会向 NULL 队列
     *    入队崩溃——所以排在按键/蓝牙初始化之前。 */
    s_queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(pc_evq_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "event queue alloc failed; app cannot run");
        return;
    }
    if (xTaskCreate(app_task, "pc_app", APP_TASK_STACK, NULL, 5,
                    &s_app_task) != pdPASS) {
        ESP_LOGE(TAG, "app task create failed; app cannot run");
        return;
    }

    /* 5. esp_timer:1 Hz 计时 / 10 s 电量 / 1.5 s 反馈 / 重连窗口
     *    (规格 §7 行 163 + §10 重连链)。回调只入队。
     *    #42:新增 s_reset_arm_timer(3 s 一次,#42 重置武装超时)。 */
    const esp_timer_create_args_t tick_args = { .callback = on_tick,
                                                .name = "pc_tick" };
    const esp_timer_create_args_t batt_args = { .callback = on_batt_poll,
                                                .name = "pc_batt" };
    const esp_timer_create_args_t fb_args = { .callback = on_feedback_timeout,
                                              .name = "pc_fb" };
    const esp_timer_create_args_t dir_args = { .callback = on_recon_directed_expired,
                                               .name = "pc_recon_dir" };
    const esp_timer_create_args_t gen_args = { .callback = on_recon_general_expired,
                                               .name = "pc_recon_gen" };
    const esp_timer_create_args_t rst_args = { .callback = on_reset_arm_expired,
                                               .name = "pc_reset_arm" };
    esp_timer_create(&tick_args, &s_tick_timer);
    esp_timer_create(&batt_args, &s_batt_timer);
    esp_timer_create(&fb_args, &s_feedback_timer);
    esp_timer_create(&dir_args, &s_recon_dir_timer);
    esp_timer_create(&gen_args, &s_recon_gen_timer);
    esp_timer_create(&rst_args, &s_reset_arm_timer);
    esp_timer_start_periodic(s_tick_timer, TICK_PERIOD_US);
    esp_timer_start_periodic(s_batt_timer, BATT_POLL_US);

    /* 5.5 电源管理(规格 §6 电源子状态机 / §10 降级):把纯逻辑电源
     *     状态机的决策落到背光/浅睡/深睡;两个背光超时定时器在其内部建立,
     *     1 s 决策搭载上方 1 Hz 事件,不新增常驻定时器。深睡唤醒路径未在本板验证(规格 §15 必测项),默认降级路径①(浅睡常驻 + 定时广播窗口;路径②依赖硬件电源键开机),编译开关与理由见 pc_power_mgr.h。
     *     线程上下文:启动阶段单线程。 */
    pc_power_mgr_init();
    pc_power_mgr_set_notify(on_power_notify);   /* 组装层接线点 */
    pc_power_mgr_set_active_backlight(s_cfg.backlight); /* 唤醒还原用 */
    pc_power_mgr_set_slot_ctx(s_cfg.slot);      /* 深睡 RTC 上下文 */

    /* 6. 按键:失败 = 无输入,设备仍可被主机侧唤醒回连(仅回连
     *    场景有意义);日志明示。 */
    s_button_ok = (bsp_button_init(on_key, NULL) == ESP_OK);
    if (!s_button_ok) ESP_LOGE(TAG, "button init failed; no input");

    /* 7. BLE HOGP:失败 -> 无线功能整体降级,其余路径照跑。 */
    s_ble_ok = (pc_ble_hid_init(on_ble_evt, NULL) == ESP_OK);
    if (!s_ble_ok) ESP_LOGE(TAG, "BLE HID init failed; radio degraded");

    /* 7.5 串口截屏模块 (#46): 仅占位初始化 (注册日志 tag);
     * 截屏在菜单第 9 项触发。依赖 USB Serial/JTAG 控制台通道,
     * 该通道在 sdkconfig.defaults 已启用 (CONFIG_ESP_CONSOLE_
     * USB_SERIAL_JTAG=y)。 */
    pc_screenshot_start();

    /* 8. FSM 初始化:无绑定首启直接进配对(规格 §1/FR-07)。 */
    bool any_bond = pc_any_slot_bound();
    pc_fsm_init(&s_fsm, any_bond, s_cfg.slot);
    s_page_src = pc_local_page_source();
    pc_speech_reset(&s_speech);

    /* 9. 启动广播策略:
     *    - 无绑定:通用广播等待首次配对;
     *    - 有绑定:对当前槽定向广播回连,窗口到时无连接则自然停。 */
    if (s_ble_ok) {
        if (!any_bond) {
            (void)pc_ble_hid_start_adv_general();
        } else {
            pc_slot_t s;
            pc_slot_load(s_cfg.slot, &s);
            if (s.bound) {
                /* #54:定向广播用槽位持久化的 addr_type。 */
                (void)pc_ble_hid_start_adv_directed(s.addr, s.addr_type);
            } else {
                (void)pc_ble_hid_start_adv_general();
            }
        }
    }

    /* 10. 初始屏显:当前模式页 + 链路 + 电量。背光/电源策略已接入(步骤 5.5),两个背光超时定时器自启动即计时。 */
    ui_show_state(s_fsm.state);
    ui_set_link(false, "");
    if (ui_lock()) {
        pc_ui_set_battery(s_battery_ok ? bsp_battery_soc() : -1);
        ui_unlock();
    }

    ESP_LOGI(TAG, "ready: i2c=%d storage=%d display=%d button=%d ble=%d "
                  "bond=%d slot=%u",
             s_i2c_ok, s_storage_ok, s_display_ok, s_button_ok, s_ble_ok,
             any_bond, (unsigned)s_cfg.slot);
}
