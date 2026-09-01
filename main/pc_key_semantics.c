// main/pc_key_semantics.c
// 键位矩阵查表实现。每一行分支都逐条对应规格
// (docs/software-design/pc-controller/requirements.md §6)的键位矩阵,
// 修改前请先对照规格行 120-133 的矩阵表。
//
// 平台无关:仅包含本模块头文件与 C11 标准库;不含任何
// ESP-IDF / LVGL / FreeRTOS 依赖,可被 host 测试直接编译。
#include "pc_key_semantics.h"

/* STANDBY(菜单页)的菜单项总数。规格 §6:待机菜单恰好 8 项
 * (PAIRING / CLEAR SLOT / SLOT / HOST PROFILE / KEY SOUND / BACKLIGHT /
 * MEDIA MODE / ABOUT)。此处仅用于注释说明;菜单导航的回绕在
 * pc_app_fsm 中实现,语义层只输出 MENU_NEXT / MENU_PREV。 */

/* 键位矩阵查表(接口说明见头文件)。
 * 实现说明:
 *   1. 事件过滤在最外层:语义层只消费 CLICK / LONG;
 *      PRESS(按下瞬间)与 DOUBLE(保留)一律返回无动作。
 *   2. SLEEP 态任何按键(任何事件)都返回唤醒动作;
 *      "首键被吞掉"的吞键逻辑在 pc_app_fsm 里执行,
 *      语义层只负责给出意图。
 *   3. 其余按 (状态 -> 按键) 两级查表,全部未列出组合落到默认分支
 *      返回无动作,保证矩阵外的组合永远不会误触发。 */
pc_action_t pc_key_map(pc_state_t st, pc_btn_t btn, pc_btn_ev_t ev, bool ble_connected)
{
    /* 事件层过滤(规格 §6:语义层只消费 CLICK / DOUBLE / LONG;
     * DOUBLE 在所有模式均"保留(无动作)")。 */
    if (ev != PC_EV_CLICK && ev != PC_EV_LONG) {
        return PC_ACT_NONE;
    }

    /* SLEEP 态:任意键仅唤醒(规格 §6 矩阵最后一行
     * "SLEEP | any | Wake only (first event consumed)")。 */
    if (st == PC_ST_SLEEP) {
        return PC_ACT_WAKE;
    }

    switch (st) {
    case PC_ST_STANDBY_HOME:
        /* 待机主页(规格 §6 矩阵行 1-3):
         *   UP   CLICK -> 打开菜单          (Enter the menu page)
         *   DOWN CLICK -> 循环切换设备槽位   (Cycle the device slot)
         *   OK   CLICK -> 已连接进演示 / 未连接进配对
         *   OK   LONG  -> 锁屏              (Lock screen) */
        switch (btn) {
        case PC_BTN_UP:
            return ev == PC_EV_CLICK ? PC_ACT_MENU_OPEN : PC_ACT_NONE;
        case PC_BTN_DOWN:
            return ev == PC_EV_CLICK ? PC_ACT_SLOT_NEXT : PC_ACT_NONE;
        case PC_BTN_OK:
            if (ev == PC_EV_CLICK) {
                return ble_connected ? PC_ACT_ENTER_PRESENT : PC_ACT_ENTER_PAIR;
            }
            if (ev == PC_EV_LONG) {
                return PC_ACT_LOCK;
            }
            return PC_ACT_NONE;
        default:
            /* 越界按键索引:防御式归一为无动作。 */
            return PC_ACT_NONE;
        }

    case PC_ST_STANDBY_MENU:
        /* 待机菜单页(规格 §6 矩阵行 4-5):
         *   UP   CLICK -> 菜单导航(上一项)
         *   DOWN CLICK -> 菜单导航(下一项)
         *   OK   CLICK -> 进入/确认所选菜单项
         *   OK   LONG  -> 回到待机主页
         * 注意:方向约定与演示模式相反——菜单里 UP 是上一项。
         * 8 项的回绕(0<->7)属于状态机职责,不在本层实现。 */
        switch (btn) {
        case PC_BTN_UP:
            return ev == PC_EV_CLICK ? PC_ACT_MENU_PREV : PC_ACT_NONE;
        case PC_BTN_DOWN:
            return ev == PC_EV_CLICK ? PC_ACT_MENU_NEXT : PC_ACT_NONE;
        case PC_BTN_OK:
            if (ev == PC_EV_CLICK) {
                return PC_ACT_MENU_CONFIRM;
            }
            if (ev == PC_EV_LONG) {
                return PC_ACT_MENU_EXIT;
            }
            return PC_ACT_NONE;
        default:
            return PC_ACT_NONE;
        }

    case PC_ST_PRESENT:
        /* 演示模式(规格 §6 矩阵行 6-7 + 规格 §1):
         *   UP   CLICK -> 下一页(注意方向:UP = next page)
         *   DOWN CLICK -> 上一页
         *   OK   CLICK -> 全屏切换(F5 / Esc 交替,记忆式)
         *   OK   LONG  -> 切换计时暂停/恢复(任务 #47;原语义为
         *                退出演示模式,现已迁移到 MEDIA 态/菜单
         *                以外的 RETURN_TO_STANDBY 路径)
         * 锁定屏与音量在演示模式被禁用(规格 §1 非目标第 3 条),
         * 因此不存在其它有效组合。
         * 注意:OK 长按退出演示模式的原语义已被 TIMER_TOGGLE 替
         * 代;演示页退出现在只通过断连 (FSM 收到 DISCONNECT) 或
         * 配对进入流程退出。 */
        switch (btn) {
        case PC_BTN_UP:
            return ev == PC_EV_CLICK ? PC_ACT_PAGE_NEXT : PC_ACT_NONE;
        case PC_BTN_DOWN:
            return ev == PC_EV_CLICK ? PC_ACT_PAGE_PREV : PC_ACT_NONE;
        case PC_BTN_OK:
            if (ev == PC_EV_CLICK) {
                return PC_ACT_FULLSCREEN_TOGGLE;
            }
            if (ev == PC_EV_LONG) {
                return PC_ACT_TIMER_TOGGLE;
            }
            return PC_ACT_NONE;
        default:
            return PC_ACT_NONE;
        }

    case PC_ST_MEDIA:
        /* 媒体模式(规格 §6 矩阵行 8-9):
         *   UP   CLICK -> 音量 +
         *   DOWN CLICK -> 音量 -
         *   OK   CLICK -> 播放/暂停
         *   OK   LONG  -> 回到待机(先回待机才能锁屏,规格 §1/§6 行 137) */
        switch (btn) {
        case PC_BTN_UP:
            return ev == PC_EV_CLICK ? PC_ACT_VOL_UP : PC_ACT_NONE;
        case PC_BTN_DOWN:
            return ev == PC_EV_CLICK ? PC_ACT_VOL_DOWN : PC_ACT_NONE;
        case PC_BTN_OK:
            if (ev == PC_EV_CLICK) {
                return PC_ACT_PLAY_PAUSE;
            }
            if (ev == PC_EV_LONG) {
                return PC_ACT_EXIT_TO_STANDBY;
            }
            return PC_ACT_NONE;
        default:
            return PC_ACT_NONE;
        }

    case PC_ST_PAIR:
        /* 配对模式(规格 §6 矩阵行 10-11):
         *   DOWN CLICK -> 循环选择槽位(槽 1 / 2 / 3)
         *   OK   CLICK -> 取消并回到待机
         *   其余(含 UP 与所有长按)无绑定。 */
        switch (btn) {
        case PC_BTN_DOWN:
            return ev == PC_EV_CLICK ? PC_ACT_SLOT_NEXT : PC_ACT_NONE;
        case PC_BTN_OK:
            return ev == PC_EV_CLICK ? PC_ACT_CANCEL : PC_ACT_NONE;
        default:
            return PC_ACT_NONE;
        }

    case PC_ST_SLEEP:
        /* 已在前面统一处理;此处不可达,仅为枚举完备性。 */
        return PC_ACT_WAKE;

    default:
        /* 越界状态值:防御式归一为无动作。 */
        return PC_ACT_NONE;
    }
}
