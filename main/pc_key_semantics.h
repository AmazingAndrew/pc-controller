// main/pc_key_semantics.h
// PC Controller 按键语义层(平台无关纯逻辑模块)。
//
// 职责:把四元组 (当前应用状态, 按键, 按键事件, BLE 连接状态) 翻译成
// 一个应用动作 (pc_action_t)。本模块是规格 §6"键位矩阵"的纯查表实现:
//   - 不持有任何状态,不产生任何副作用;
//   - 不做状态转移(转移属于 pc_app_fsm);
//   - 不做菜单回绕(回绕属于 pc_app_fsm);
//   - 不解析锁屏组合键(组合键属于 pc_host_profiles,由组装层注入)。
//
// 事实源:
//   - 五模式 / 两层待机 / 键位矩阵:
//     docs/software-design/pc-controller/requirements.md §6
//   - 按键与事件枚举与 components/bsp/include/bsp_button.h 的
//     bsp_btn_t / bsp_btn_ev_t 逐值同构,平台层可直接以数值透传,
//     无需做枚举转换。
//
// 平台无关性:仅依赖 C11 标准库(<stdbool.h>),禁止包含任何
// ESP-IDF / LVGL / FreeRTOS 头。可被以下命令直接编译:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 供 host 端纯 assert 测试使用(测试范式见 tests/test_ui_pixel_math.c)。
#pragma once

#include <stdbool.h>

/* 按键索引。与 bsp_button.h 的 bsp_btn_t 同构,值一一对应:
 *   PC_BTN_UP   = 0 = BSP_BTN_UP
 *   PC_BTN_DOWN = 1 = BSP_BTN_DOWN
 *   PC_BTN_OK   = 2 = BSP_BTN_OK
 * 三个按键共用同一个 ADC 通道,物理上不可能同时按下,
 * 因此全部交互均为单键手势(规格 §3)。 */
typedef enum {
    PC_BTN_UP = 0,
    PC_BTN_DOWN,
    PC_BTN_OK,
} pc_btn_t;

/* 按键事件类型。与 bsp_button.h 的 bsp_btn_ev_t 同构,值一一对应:
 *   PC_EV_PRESS  = 0 = BSP_BTN_PRESS
 *   PC_EV_CLICK  = 1 = BSP_BTN_CLICK
 *   PC_EV_DOUBLE = 2 = BSP_BTN_DOUBLE
 *   PC_EV_LONG   = 3 = BSP_BTN_LONG
 * 按确认基线(规格 §6 偏差记录第 1 条),DOUBLE 在所有模式下均不绑定
 * 任何动作(黑屏功能取消),保留在事件词汇表中仅为词汇完整性。 */
typedef enum {
    PC_EV_PRESS = 0,  /* 按下瞬间。语义层不消费(规格 §6:语义层只消费
                       * CLICK / DOUBLE / LONG,PRESS 供低延迟场景预留)。 */
    PC_EV_CLICK,      /* 单击(按下并抬起)。绝大多数模式的主手势。 */
    PC_EV_DOUBLE,     /* 双击。保留,任何模式下均返回 PC_ACT_NONE。 */
    PC_EV_LONG,       /* 长按(>= 800 ms,阈值由 BSP 判定,见规格 §1/FR-03)。 */
} pc_btn_ev_t;

/* 应用状态(五模式 + 待机的两层子状态)。与规格 §6 状态机一致:
 *   - STANDBY 分两层:待机主页 (PC_ST_STANDBY_HOME) 与菜单页
 *     (PC_ST_STANDBY_MENU),两者都是"待机",按键语义不同。
 *   - SLEEP 是电源侧进入睡眠后的应用态;唤醒首键只唤醒、不执行功能
 *     (规格 §6 转移表最后一行)。 */
typedef enum {
    PC_ST_STANDBY_HOME = 0,  /* 待机主页 */
    PC_ST_STANDBY_MENU,      /* 待机菜单页(8 项菜单,见规格 §6) */
    PC_ST_PRESENT,           /* 演示模式:翻页 + 全屏切换 + 计时 */
    PC_ST_MEDIA,             /* 媒体模式:音量 + 播放/暂停 */
    PC_ST_PAIR,              /* 配对模式:槽位选择 + 显示 6 位配对码 */
    PC_ST_SLEEP,             /* 睡眠(电源子状态机已降级,应用侧等待唤醒) */
} pc_state_t;

/* 应用动作。按键语义层的输出、应用状态机 (pc_app_fsm) 的输入。
 * 命名约定:动作表达"用户意图",不表达具体 HID 键码;HID 细节由
 * pc_app_fsm 翻译成 effect(见 pc_app_fsm.h)。 */
typedef enum {
    PC_ACT_NONE = 0,          /* 无动作(键位矩阵中的 "保留"/"-" 项) */
    PC_ACT_WAKE,              /* SLEEP 态任意键:仅唤醒,首键被吞掉 */
    PC_ACT_MENU_OPEN,         /* 待机主页打开菜单 */
    PC_ACT_MENU_NEXT,         /* 菜单选择下一项(回绕在 FSM) */
    PC_ACT_MENU_PREV,         /* 菜单选择上一项(回绕在 FSM) */
    PC_ACT_MENU_CONFIRM,      /* 确认当前菜单项(具体行为在 FSM 按项分发) */
    PC_ACT_MENU_EXIT,         /* 菜单长按退出,回到待机主页 */
    PC_ACT_ENTER_PRESENT,     /* 进入演示模式(需 BLE 已连接) */
    PC_ACT_ENTER_PAIR,        /* 进入配对模式 */
    PC_ACT_ENTER_MEDIA,       /* 进入媒体模式 */
    PC_ACT_PAGE_NEXT,         /* 演示模式:下一页(注意:UP 键 = 下一页) */
    PC_ACT_PAGE_PREV,         /* 演示模式:上一页(注意:DOWN 键 = 上一页) */
    PC_ACT_FULLSCREEN_TOGGLE, /* 演示模式:全屏切换(记忆式翻转,见规格 §6) */
    PC_ACT_TIMER_TOGGLE,      /* 演示模式:OK 长按切换计时暂停/恢复(任务 #47) */
    PC_ACT_LOCK,              /* 一键锁屏(仅待机主页可直达,规格 §1/FR-03) */
    PC_ACT_VOL_UP,            /* 媒体模式:音量 + */
    PC_ACT_VOL_DOWN,          /* 媒体模式:音量 - */
    PC_ACT_PLAY_PAUSE,        /* 媒体模式:播放/暂停 */
    PC_ACT_SLOT_NEXT,         /* 切换下一个设备槽位(1->2->3->1,规格 §1) */
    PC_ACT_CANCEL,            /* 配对模式:取消并回到待机 */
    PC_ACT_EXIT_TO_STANDBY,   /* 长按退出当前模式,回到待机主页 */
} pc_action_t;

/* 键位矩阵查表:把 (状态, 按键, 事件, 连接状态) 翻译成应用动作。
 *
 * 用途:按键语义层的唯一入口;组装层收到 BSP 按键回调后先经本函数
 *       翻译,再把动作投递给应用状态机。
 * 参数:
 *   st            当前应用状态。越界值按"无动作"处理(防御式)。
 *   btn           按键索引。越界值按"无动作"处理(防御式)。
 *   ev            按键事件。PRESS / DOUBLE 恒返回 PC_ACT_NONE。
 *   ble_connected 当前是否有活动 BLE 连接。只影响待机主页的
 *                 "OK 单击"分支(已连接 -> 进演示;未连接 -> 进配对,
 *                 规格 §6 转移表第 2/3 行)。
 * 返回值:翻译得到的动作;任何未绑定组合返回 PC_ACT_NONE。
 * 失败值:无(纯查表,不会失败;非法输入归一为 PC_ACT_NONE)。
 * 线程上下文:可在任意任务上下文调用;纯函数,无共享状态,无阻塞,
 *            无内存分配。典型调用方是应用任务(消费统一事件队列)。
 * 内存所有权:无。
 * 依据:规格 §6 完整键位矩阵(行 120-133)与状态转移表(行 104-116)。 */
pc_action_t pc_key_map(pc_state_t st, pc_btn_t btn, pc_btn_ev_t ev, bool ble_connected);
