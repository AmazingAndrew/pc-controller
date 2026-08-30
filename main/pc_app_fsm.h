// main/pc_app_fsm.h
// PC Controller 应用状态机(平台无关纯逻辑模块),effect 驱动设计。
//
// 设计模型:
//   按键语义层 (pc_key_semantics) 输出动作 -> 本状态机消费动作并:
//     1. 更新内部状态(模式、全屏记忆、菜单选择、槽位等);
//     2. 向调用方"吐出"一组 effect (pc_effect_t)。
//   effect 是纯数据描述,所有平台动作(发 HID 报告、起停广播、
//   写 NVS、UI 反馈、复位计时器)都由组装层解释 effect 后执行。
//   这样状态机本身零副作用、零依赖,可被 host 纯 assert 测试
//   完整覆盖(规格 §13)。
//
// 事实源:规格 §6 状态转移表(行 104-116)与 §1 核心基线、
//         §10 断连降级策略。所有转移在实现处逐行注释引用。
//
// 平台无关性:仅依赖 C11 标准库与 pc_key_semantics.h / pc_hid_reports.h
// (后两者同为纯逻辑模块),禁止包含任何 ESP-IDF / LVGL / FreeRTOS 头。
// 可被以下命令直接编译:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pc_key_semantics.h"

/* effect 类型。状态机对外吐出的"平台动作意图",由组装层执行。
 * 命名约定:PC_FX_ 前缀(FX = effect)。 */
typedef enum {
    PC_FX_HID_KEY = 0,          /* 发送一帧键盘报告(修饰位 + 键码),
                                 * 组装层负责随后补发空报告释放按键
                                 * (FR-01:无卡键)。参数:arg.key。 */
    PC_FX_HID_CONSUMER,         /* 发送一帧 Consumer Page 报告(16 位
                                 * usage)。参数:arg.usage。 */
    PC_FX_PAGE_ENTER_FULLSCREEN,/* 逻辑进入全屏:页码来源复位到 1
                                 * (FR-12),无参数。 */
    PC_FX_PAGE_EXIT_FULLSCREEN, /* 逻辑退出全屏:页码来源停止计数,
                                 * 无参数。 */
    PC_FX_PAGE_STEP,            /* 页码步进。参数:arg.page_delta(±1)。 */
    PC_FX_TIMER_RESET,          /* 演讲计时器清零(进入演示模式,
                                 * FR-05),无参数。 */
    PC_FX_START_PAIR,           /* 进入配对:启动(定向/通用)广播 +
                                 * 屏显 6 位配对码通道,无参数。 */
    PC_FX_STOP_PAIR,            /* 退出配对:停止配对广播,无参数。 */
    PC_FX_SLOT_SWITCH,          /* 切换设备槽位:先优雅断开当前连接
                                 * 再对目标槽做定向广播(规格 §1/FR-06)。
                                 * 参数:arg.slot(新槽位索引,0 基)。 */
    PC_FX_SLOT_CLEAR,           /* 清除槽位绑定(菜单 CLEAR SLOT,
                                 * FR-08)。参数:arg.slot。 */
    PC_FX_SAVE_CFG,             /* 配置变更需持久化到 NVS(规格 §8),
                                 * 无参数;组装层决定写哪些键。 */
    PC_FX_SHOW_FEEDBACK_LOCK,   /* 锁屏触发反馈:组装层据此解析当前
                                 * 槽位主机配置的锁屏组合键
                                 * (pc_host_profiles)并发送,同时给
                                 * UI/蜂鸣反馈。无参数。 */
    PC_FX_DISCONNECT,           /* 主动断开当前连接,无参数。 */
    PC_FX_ADV_RECONNECT,        /* 断连后的重连广播序列(定向 30 s ->
                                 * 通用 2 min,规格 §10),无参数。 */
    PC_FX_ENTER_MEDIA,          /* 进入媒体模式(供组装层做模式切换
                                 * 的附加动作,如音效),无参数。 */
} pc_fx_t;

/* 单个 effect。联合体按 type 选用成员:
 *   PC_FX_HID_KEY      -> arg.key(修饰位 + 键码)
 *   PC_FX_HID_CONSUMER -> arg.usage(16 位 HID usage)
 *   PC_FX_SLOT_SWITCH / PC_FX_SLOT_CLEAR -> arg.slot(0 基槽位索引)
 *   PC_FX_PAGE_STEP    -> arg.page_delta(+1 / -1)
 *   其余 type          -> 无参数(联合体保留上一次残值,调用方勿读)。 */
typedef struct {
    pc_fx_t type;
    union {
        struct {
            uint8_t mods;    /* 修饰位组合(见 pc_hid_reports.h PC_MOD_*) */
            uint8_t keycode; /* 键码(见 pc_hid_reports.h PC_KEY_*) */
        } key;
        uint16_t usage;   /* Consumer Page usage(见 PC_USAGE_*) */
        uint8_t slot;     /* 槽位索引,取值 0..槽数-1 */
        int page_delta;   /* 页码步进量,+1 或 -1 */
    } arg;
} pc_effect_t;

/* BLE 侧事件。由组装层把 NimBLE GAP/SM 事件归一后喂给状态机。 */
typedef enum {
    PC_BLE_CONNECTED = 0,   /* 连接建立 */
    PC_BLE_DISCONNECTED,    /* 连接断开(含监督超时,规格 §10) */
    PC_BLE_PAIR_OK,         /* 配对/绑定成功 */
    PC_BLE_PAIR_FAIL,       /* 配对失败(如配对码输错) */
} pc_ble_evt_t;

/* 应用状态机实例。全部字段由组装层在单一应用任务中读写
 * (规格 §7:一个应用任务消费统一事件队列并驱动 FSM),
 * 因此本结构体内部不做任何并发保护。 */
typedef struct {
    pc_state_t state;          /* 当前应用状态(五模式 + 待机两层) */
    bool fullscreen;           /* 全屏记忆位:全屏切换为记忆式翻转,
                                * 第一次进、第二次退(规格 §1/FR-02)。 */
    uint8_t menu_sel;          /* 菜单当前选中项索引,取值 0..7,
                                * 8 项回绕(规格 §6)。 */
    uint8_t slot;              /* 当前/所选设备槽位索引,0..槽数-1 */
    uint8_t slot_count;        /* 槽位总数,固定 3(规格 §1:三设备槽位) */
    bool wake_key_pending;     /* 睡眠唤醒首键吞键标志(规格 §6 转移表
                                * 最后一行:首键只唤醒不执行功能)。 */
    bool any_bond;             /* 是否存在任一槽位的历史绑定;
                                * 无绑定上电直接进配对模式(规格 §1/FR-07)。 */
} pc_fsm_t;

/* 初始化状态机。
 * 参数:
 *   f            待初始化实例,不可为 NULL(为 NULL 时直接返回)。
 *   any_bond     NVS 中是否存在任一早有绑定的槽位。
 *                false -> 初始状态为配对模式(规格 §1/FR-07:
 *                无绑定首次上电自动进配对);
 *                true  -> 初始状态为待机主页。
 *   initial_slot 初始槽位索引(取自 NVS 配置,规格 §8)。
 *                内部按 3 槽取模归一,防御越界配置。
 * 返回值:无。失败值:无。
 * 线程上下文:仅由组装层初始化路径调用一次(启动阶段,单线程)。
 * 内存所有权:不分配内存;实例由调用方提供并持有。 */
void pc_fsm_init(pc_fsm_t *f, bool any_bond, uint8_t initial_slot);

/* 消费一个应用动作,推进状态机并吐出 effect 序列。
 * 参数:
 *   f             状态机实例,不可为 NULL。
 *   a             按键语义层输出的动作。
 *   ble_connected 当前 BLE 连接状态;仅用于"进入演示模式"的守卫
 *                 (未连接时拒绝进入,与键位矩阵语义一致)。
 *   out           effect 输出缓冲。允许 NULL(此时仅返回应产生的
 *                 数量,不写入)。
 *   cap           out 容量(可写入的 pc_effect_t 个数)。
 * 返回值:本次动作应产生的 effect 总数(可能大于 cap)。
 *         写入数量 = min(总数, cap);若返回值 > cap 表示输出被截断,
 *         调用方应加大缓冲(实际最大为 3,推荐 cap >= 4)。
 * 失败值:参数非法返回 0。
 * 线程上下文:应用任务(单消费者),无阻塞,无内存分配。
 * 内存所有权:只写调用方提供的 out 缓冲。 */
int pc_fsm_on_action(pc_fsm_t *f, pc_action_t a, bool ble_connected, pc_effect_t *out, int cap);

/* 消费一个 BLE 侧事件,推进状态机并吐出 effect 序列。
 * 参数与返回约定与 pc_fsm_on_action 相同。
 * 行为要点(规格 §6/§10):
 *   - 断连时若处于演示/媒体模式 -> 回到待机主页(若处于全屏先退出
 *     全屏,避免主机侧停留在演示全屏);
 *   - 配对成功 -> 回到待机主页并更新 any_bond;
 *   - 配对失败 -> 停留在配对模式(屏显由组装层反馈)。 */
int pc_fsm_on_ble(pc_fsm_t *f, pc_ble_evt_t ev, pc_effect_t *out, int cap);

/* 消费电源侧状态变更(睡眠/唤醒)。
 * 参数:
 *   to_sleep true  -> 电源子状态机已降级到睡眠:应用态进入 SLEEP,
 *                     置唤醒吞键标志,清除全屏记忆(睡眠必然离开演示
 *                     场景;连接由组装层随睡眠策略处理)。
 *            false -> 显式唤醒(例如电源键/非首键路径):回到待机主页。
 * 返回值/失败值/线程上下文:与 pc_fsm_on_action 相同。 */
int pc_fsm_on_power(pc_fsm_t *f, bool to_sleep, pc_effect_t *out, int cap);
