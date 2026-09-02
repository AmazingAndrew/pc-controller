// tests/test_pc_key_semantics.c
// 按键语义层 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 (docs/software-design/pc-controller/requirements.md)
// §6 键位矩阵(行 120-133)与偏差记录第 1 条。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_key_semantics.c main/pc_key_semantics.c
#include <assert.h>

#include "pc_key_semantics.h"

int main(void)
{
    /* ======== STANDBY (home) —— 规格 §6 矩阵行 1-3 ======== */

    /* 行 1:UP 短按 -> 打开菜单;长按无绑定("-")。 */
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_MENU_OPEN);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_UP, PC_EV_CLICK, false) == PC_ACT_MENU_OPEN);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_UP, PC_EV_LONG, true) == PC_ACT_NONE);

    /* 行 2:DOWN 短按 -> 循环切换设备槽位;长按无绑定。 */
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_DOWN, PC_EV_CLICK, true) == PC_ACT_SLOT_NEXT);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_DOWN, PC_EV_CLICK, false) == PC_ACT_SLOT_NEXT);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_DOWN, PC_EV_LONG, true) == PC_ACT_NONE);

    /* 行 3:OK 短按 -> 已连接进演示 / 未连接进配对(连接状态分叉);
     * OK 长按 -> 锁屏(仅待机可直达,规格 §1/FR-03)。 */
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_ENTER_PRESENT);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, PC_EV_CLICK, false) == PC_ACT_ENTER_PAIR);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_LOCK);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, PC_EV_LONG, false) == PC_ACT_LOCK);

    /* PRESS 事件语义层不消费(规格 §6:只消费 CLICK/DOUBLE/LONG)。 */
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_UP, PC_EV_PRESS, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_DOWN, PC_EV_PRESS, false) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, PC_EV_PRESS, true) == PC_ACT_NONE);

    /* ======== STANDBY (menu) —— 规格 §6 矩阵行 4-5 ======== */

    /* 行 4:UP / DOWN 短按 -> 8 项菜单导航(回绕归状态机)。
     * 实现约定:菜单里 UP = 上一项、DOWN = 下一项。 */
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_MENU_PREV);
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_DOWN, PC_EV_CLICK, true) == PC_ACT_MENU_NEXT);
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_UP, PC_EV_LONG, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_DOWN, PC_EV_LONG, true) == PC_ACT_NONE);

    /* 行 5:OK 短按 -> 确认所选菜单项;OK 长按 -> 回待机主页。 */
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_MENU_CONFIRM);
    assert(pc_key_map(PC_ST_STANDBY_MENU, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_MENU_EXIT);

    /* ======== PRESENT —— 规格 §6 矩阵行 6-7 + §1 方向约定 ======== */

    /* 行 6:UP = 下一页、DOWN = 上一页(规格 §1)。 */
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_PAGE_NEXT);
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_DOWN, PC_EV_CLICK, true) == PC_ACT_PAGE_PREV);
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_UP, PC_EV_LONG, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_DOWN, PC_EV_LONG, true) == PC_ACT_NONE);

    /* 行 7:OK 短按 -> 全屏切换;OK 长按 -> 切换计时暂停/恢复(任务 #47)。 */
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_FULLSCREEN_TOGGLE);
    /* 任务 #47:OK 长按在演示模式切换计时暂停/恢复(不再退出 PRESENT)。 */
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_TIMER_TOGGLE);

    /* 锁屏在演示模式被禁用(规格 §1 非目标第 3 条):
     * 任何键任何事件都不得给出 PC_ACT_LOCK。 */
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_UP, PC_EV_LONG, false) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PRESENT, PC_BTN_DOWN, PC_EV_LONG, false) == PC_ACT_NONE);

    /* ======== MEDIA —— 规格 §6 矩阵行 8-9 ======== */

    /* 行 8:UP = 音量 +、DOWN = 音量 -(Consumer Page)。 */
    assert(pc_key_map(PC_ST_MEDIA, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_VOL_UP);
    assert(pc_key_map(PC_ST_MEDIA, PC_BTN_DOWN, PC_EV_CLICK, true) == PC_ACT_VOL_DOWN);

    /* 行 9:OK 短按 = 播放/暂停;OK 长按先回待机才能锁屏(行 137),
     * 因此 MEDIA 内长按只能给出 EXIT,绝无 LOCK。 */
    assert(pc_key_map(PC_ST_MEDIA, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_PLAY_PAUSE);
    assert(pc_key_map(PC_ST_MEDIA, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_EXIT_TO_STANDBY);
    assert(pc_key_map(PC_ST_MEDIA, PC_BTN_OK, PC_EV_LONG, false) == PC_ACT_EXIT_TO_STANDBY);

    /* ======== PAIR —— 规格 §6 矩阵行 10-11 ======== */

    /* 行 10:DOWN 短按 -> 循环选择槽位;行 11:OK 短按 -> 取消回待机。 */
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_DOWN, PC_EV_CLICK, true) == PC_ACT_SLOT_NEXT);
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_CANCEL);

    /* 其余组合无绑定:UP 全事件、OK 长按、DOWN 长按。 */
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_UP, PC_EV_LONG, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PAIR, PC_BTN_DOWN, PC_EV_LONG, true) == PC_ACT_NONE);

    /* ======== SLEEP —— 规格 §6 矩阵最后一行 ======== */

    /* "SLEEP | any | Wake only":任意键的 CLICK / LONG 均只唤醒。 */
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_UP, PC_EV_CLICK, true) == PC_ACT_WAKE);
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_DOWN, PC_EV_CLICK, false) == PC_ACT_WAKE);
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_WAKE);
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_OK, PC_EV_LONG, true) == PC_ACT_WAKE);
    /* PRESS / DOUBLE 在事件层过滤,恒无动作(首键吞掉在状态机)。 */
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_UP, PC_EV_PRESS, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_SLEEP, PC_BTN_UP, PC_EV_DOUBLE, true) == PC_ACT_NONE);

    /* ======== DOUBLE 全状态无绑定 —— 规格 §1 非目标 / §6 偏差记录 1 ======== */
    {
        pc_state_t st;
        pc_btn_t btn;
        for (st = PC_ST_STANDBY_HOME; st <= PC_ST_SLEEP; st++) {
            for (btn = PC_BTN_UP; btn <= PC_BTN_OK; btn++) {
                assert(pc_key_map(st, btn, PC_EV_DOUBLE, true) == PC_ACT_NONE);
                assert(pc_key_map(st, btn, PC_EV_DOUBLE, false) == PC_ACT_NONE);
            }
        }
    }

    /* ======== 越界 / 未知枚举:防御式归一为无动作 ======== */
    assert(pc_key_map((pc_state_t)99, PC_BTN_OK, PC_EV_CLICK, true) == PC_ACT_NONE);
    assert(pc_key_map((pc_state_t)-1, PC_BTN_UP, PC_EV_LONG, false) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_STANDBY_HOME, (pc_btn_t)99, PC_EV_CLICK, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_PRESENT, (pc_btn_t)-1, PC_EV_CLICK, true) == PC_ACT_NONE);
    assert(pc_key_map(PC_ST_STANDBY_HOME, PC_BTN_OK, (pc_btn_ev_t)99, true) == PC_ACT_NONE);

    return 0;
}
