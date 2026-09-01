// tests/test_pc_app_fsm.c
// 应用状态机 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 (docs/software-design/pc-controller/requirements.md)
// §6 状态转移表(行 104-116)、键位矩阵(行 120-133)、
// §1/FR-02/FR-05/FR-06/FR-07/FR-12、§10 断连降级。
// 编译命令(状态机引用 pc_hid_reports.h 的键码常量,需一并链接):
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_app_fsm.c main/pc_app_fsm.c main/pc_hid_reports.c
#include <assert.h>
#include <stddef.h>

#include "pc_app_fsm.h"
#include "pc_hid_reports.h"

/* 效果缓冲容量:接口约定实际最大为 3,推荐 >= 4。 */
#define FX_CAP 8

int main(void)
{
    pc_fsm_t f;
    pc_effect_t fx[FX_CAP];
    int n;

    /* ======== 初始化 —— 规格 §1/FR-07 ======== */

    /* 无绑定首次上电 -> 直接进配对模式。 */
    pc_fsm_init(&f, false, 0);
    assert(f.state == PC_ST_PAIR);
    assert(f.slot_count == 3);
    assert(!f.any_bond);
    assert(!f.fullscreen);
    assert(!f.wake_key_pending);

    /* 有绑定 -> 待机主页;初始槽位取自配置。 */
    pc_fsm_init(&f, true, 2);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(f.slot == 2);
    assert(f.any_bond);

    /* 越界槽位配置按 3 槽取模归一(防御式)。 */
    pc_fsm_init(&f, true, 7);
    assert(f.slot == 1); /* 7 % 3 == 1 */

    /* ======== 待机主页 -> 菜单 / 演示 / 配对 / 锁屏 ======== */

    /* 转移表行 1:STANDBY (home) UP -> MENU。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_MENU);
    assert(f.menu_sel == 0);

    /* 转移表行 2:OK 单击(已连接)-> PRESENT,
     * 产生 TIMER_RESET + PAGE_ENTER_FULLSCREEN(FR-05 / FR-12);
     * 进入时不发送 F5(首次全屏由第一次 OK 单击触发)。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    assert(n == 2);
    assert(f.state == PC_ST_PRESENT);
    assert(fx[0].type == PC_FX_TIMER_RESET);
    assert(fx[1].type == PC_FX_PAGE_ENTER_FULLSCREEN);
    assert(!f.fullscreen);

    /* 守卫:未连接时拒绝进入演示(与键位矩阵语义一致)。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, false, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* 转移表行 3:未连接时待机主页进配对,吐 START_PAIR。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PAIR, false, fx, FX_CAP);
    assert(n == 1);
    assert(f.state == PC_ST_PAIR);
    assert(fx[0].type == PC_FX_START_PAIR);

    /* 锁屏:仅待机主页产生 SHOW_FEEDBACK_LOCK,状态不变。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_LOCK, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_SHOW_FEEDBACK_LOCK);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* 待机主页 DOWN -> 槽位循环切换(1->2->3->1,规格 §6 行 123)。 */
    pc_fsm_init(&f, true, 2); /* 从槽 3(索引 2)开始 */
    n = pc_fsm_on_action(&f, PC_ACT_SLOT_NEXT, true, fx, FX_CAP);
    assert(n == 1);
    assert(f.slot == 0); /* 回绕到槽 1 */
    assert(fx[0].type == PC_FX_SLOT_SWITCH);
    assert(fx[0].arg.slot == 0);

    /* ======== 菜单导航:10 项回绕 0..9(规格 §6 行 135 + #42 RESET BLE + #46 SCREENSHOT) ======== */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    assert(f.menu_sel == 0);
    /* DOWN 导航 10 次:0->1->...->9->0(回绕)。 */
    for (int i = 1; i <= 10; i++) {
        n = pc_fsm_on_action(&f, PC_ACT_MENU_NEXT, true, fx, FX_CAP);
        assert(n == 0);
        assert(f.menu_sel == (uint8_t)(i % 10));
    }
    /* UP 导航:0->9(反向回绕)。 */
    n = pc_fsm_on_action(&f, PC_ACT_MENU_PREV, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.menu_sel == 9);

    /* ======== 菜单确认分发(规格 §6 行 135 的 8 项 + #42 项 8 + #46 项 9) ======== */

    /* 项 0 (1) PAIRING -> 进配对 + START_PAIR(转移表行 5)。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 0;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 1);
    assert(f.state == PC_ST_PAIR);
    assert(fx[0].type == PC_FX_START_PAIR);

    /* 项 1 (2) CLEAR SLOT -> 清除当前槽,停留菜单。 */
    pc_fsm_init(&f, true, 1);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 1;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_SLOT_CLEAR);
    assert(fx[0].arg.slot == 1); /* 当前槽 */
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 项 2 (3) SLOT -> 切换下一槽,停留菜单。 */
    pc_fsm_init(&f, true, 1);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 2;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 1);
    assert(f.slot == 2);
    assert(fx[0].type == PC_FX_SLOT_SWITCH);
    assert(fx[0].arg.slot == 2);
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 项 3/4/5 配置类 -> SAVE_CFG,停留菜单。 */
    for (int item = 3; item <= 5; item++) {
        pc_fsm_init(&f, true, 0);
        pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
        f.menu_sel = (uint8_t)item;
        n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
        assert(n == 1);
        assert(fx[0].type == PC_FX_SAVE_CFG);
        assert(f.state == PC_ST_STANDBY_MENU);
    }

    /* 项 6 (7) MEDIA MODE -> 进媒体模式(转移表行 4)。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 6;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 1);
    assert(f.state == PC_ST_MEDIA);
    assert(fx[0].type == PC_FX_ENTER_MEDIA);

    /* 项 7 (8) ABOUT -> 仅屏显,零 effect。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 7;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 项 8 (9) #42 RESET BLE -> 状态机不吐 effect(组装层
     * on_menu_confirm_apply 按 menu_sel==8 自行处理武装/执行) */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 8;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 项 9 (10) #46 SCREENSHOT -> 状态机不吐 effect(组装层
     * on_menu_confirm_apply 按 menu_sel==9 自行调 pc_screenshot_capture)。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 9;
    n = pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 转移表行 6:菜单长按 -> 待机主页。 */
    n = pc_fsm_on_action(&f, PC_ACT_MENU_EXIT, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* ======== 演示模式:OK 长按切换计时器暂停/恢复(任务 #47) ========
     * 状态机不修改自身状态, 仅吐 1 个 PC_FX_TIMER_TOGGLE effect;
     * 暂停位的真存在组装层持有的 pc_speech_timer_t.paused 上,
     * 由 PC_FX_TIMER_TOGGLE 触发 pc_speech_set_paused() 翻转。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_TIMER_TOGGLE, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_TIMER_TOGGLE);
    assert(f.state == PC_ST_PRESENT); /* 状态未变, 仍处于演示 */
    /* 再次 toggle -> 仍吐 1 effect。 */
    n = pc_fsm_on_action(&f, PC_ACT_TIMER_TOGGLE, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_TIMER_TOGGLE);
    /* 非演示状态下 TIMER_TOGGLE 无效。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_TIMER_TOGGLE, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* ======== 演示模式:翻页 HID 键码 + 页码步进 ======== */

    /* PAGE_NEXT:发方向"上" 0x52(UP = next,规格 §1)+ 步进 +1。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_PAGE_NEXT, true, fx, FX_CAP);
    assert(n == 2);
    assert(fx[0].type == PC_FX_HID_KEY);
    assert(fx[0].arg.key.mods == 0);
    assert(fx[0].arg.key.keycode == 0x52); /* PC_KEY_UP */
    assert(fx[1].type == PC_FX_PAGE_STEP);
    assert(fx[1].arg.page_delta == 1);

    /* PAGE_PREV:发方向"下" 0x51 + 步进 -1。 */
    n = pc_fsm_on_action(&f, PC_ACT_PAGE_PREV, true, fx, FX_CAP);
    assert(n == 2);
    assert(fx[0].type == PC_FX_HID_KEY);
    assert(fx[0].arg.key.keycode == 0x51); /* PC_KEY_DOWN */
    assert(fx[1].type == PC_FX_PAGE_STEP);
    assert(fx[1].arg.page_delta == -1);

    /* ======== 全屏切换:记忆式翻转(规格 §1/FR-02) ======== */

    /* 首次 OK 单击 -> 三连发跨平台全屏(#57:Win F5 / macOS PPT Cmd+Shift+Enter
     * / macOS Keynote Opt+Cmd+P,各帧间留 200 ms 由组装层插入) + 进全屏,记忆位置位。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_FULLSCREEN_TOGGLE, true, fx, FX_CAP);
    assert(n == 4);
    assert(fx[0].type == PC_FX_HID_KEY);
    assert(fx[0].arg.key.mods == 0);
    assert(fx[0].arg.key.keycode == 0x3E); /* PC_KEY_F5 */
    assert(fx[1].type == PC_FX_HID_KEY);
    assert(fx[1].arg.key.mods == (PC_MOD_LGUI | PC_MOD_LSHIFT)); /* 0x0A */
    assert(fx[1].arg.key.keycode == 0x28); /* PC_KEY_RETURN */
    assert(fx[2].type == PC_FX_HID_KEY);
    assert(fx[2].arg.key.mods == (PC_MOD_LGUI | PC_MOD_LALT)); /* 0x0C */
    assert(fx[2].arg.key.keycode == 0x13); /* PC_KEY_P */
    assert(fx[3].type == PC_FX_PAGE_ENTER_FULLSCREEN);
    assert(f.fullscreen);

    /* 第二次 -> 发 Esc(0x29) + 退全屏,记忆位复位。 */
    n = pc_fsm_on_action(&f, PC_ACT_FULLSCREEN_TOGGLE, true, fx, FX_CAP);
    assert(n == 2);
    assert(fx[0].type == PC_FX_HID_KEY);
    assert(fx[0].arg.key.keycode == 0x29); /* PC_KEY_ESC */
    assert(fx[1].type == PC_FX_PAGE_EXIT_FULLSCREEN);
    assert(!f.fullscreen);

    /* 翻转记忆跨"进入-退出演示"周期复位:重新进入后第一次仍是
     * 三连发 F5 / Cmd+Shift+Enter / Opt+Cmd+P。 */
    n = pc_fsm_on_action(&f, PC_ACT_EXIT_TO_STANDBY, true, fx, FX_CAP);
    assert(f.state == PC_ST_STANDBY_HOME);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_FULLSCREEN_TOGGLE, true, fx, FX_CAP);
    assert(n == 4);
    assert(fx[0].arg.key.keycode == 0x3E); /* 仍是 F5 */
    assert(fx[1].arg.key.keycode == 0x28); /* Cmd+Shift+Enter */
    assert(fx[2].arg.key.keycode == 0x13); /* Opt+Cmd+P */
    assert(fx[3].type == PC_FX_PAGE_ENTER_FULLSCREEN);

    /* 全屏中长按退出:先发 Esc 键(B1 修复:与全屏切换退路径对齐,
     * 避免主机停留在演示全屏),再吐退全屏 effect 回待机。 */
    n = pc_fsm_on_action(&f, PC_ACT_EXIT_TO_STANDBY, true, fx, FX_CAP);
    assert(n == 2);
    assert(fx[0].type == PC_FX_HID_KEY);
    assert(fx[0].arg.key.mods == 0);
    assert(fx[0].arg.key.keycode == 0x29); /* PC_KEY_ESC */
    assert(fx[1].type == PC_FX_PAGE_EXIT_FULLSCREEN);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(!f.fullscreen);

    /* ======== 媒体模式:Consumer usage 翻译 ======== */

    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    f.menu_sel = 6;
    pc_fsm_on_action(&f, PC_ACT_MENU_CONFIRM, true, fx, FX_CAP);
    assert(f.state == PC_ST_MEDIA);

    n = pc_fsm_on_action(&f, PC_ACT_VOL_UP, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_HID_CONSUMER);
    assert(fx[0].arg.usage == 0xE9); /* PC_USAGE_VOL_UP */

    n = pc_fsm_on_action(&f, PC_ACT_VOL_DOWN, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].arg.usage == 0xEA); /* PC_USAGE_VOL_DOWN */

    n = pc_fsm_on_action(&f, PC_ACT_PLAY_PAUSE, true, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].arg.usage == 0xCD); /* PC_USAGE_PLAY_PAUSE */

    /* 转移表行 8:媒体长按 -> 待机。 */
    n = pc_fsm_on_action(&f, PC_ACT_EXIT_TO_STANDBY, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* ======== 配对模式:槽位循环 / 取消 / 配对成功 ======== */

    /* 配对模式 DOWN -> 槽位循环(规格 §6 行 10)。 */
    pc_fsm_init(&f, false, 0); /* 无绑定上电直接处于配对 */
    assert(f.state == PC_ST_PAIR);
    n = pc_fsm_on_action(&f, PC_ACT_SLOT_NEXT, false, fx, FX_CAP);
    assert(n == 1);
    assert(f.slot == 1);
    assert(fx[0].type == PC_FX_SLOT_SWITCH);
    assert(fx[0].arg.slot == 1);

    /* 转移表行 9:配对取消 -> 待机主页 + STOP_PAIR。 */
    n = pc_fsm_on_action(&f, PC_ACT_CANCEL, false, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_STOP_PAIR);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* 配对成功 -> 待机主页、any_bond 置位、STOP_PAIR + SAVE_CFG。 */
    pc_fsm_init(&f, false, 0);
    assert(f.state == PC_ST_PAIR);
    n = pc_fsm_on_ble(&f, PC_BLE_PAIR_OK, fx, FX_CAP);
    assert(n == 2);
    assert(fx[0].type == PC_FX_STOP_PAIR);
    assert(fx[1].type == PC_FX_SAVE_CFG);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(f.any_bond);

    /* 配对失败 -> 停留配对模式,零 effect。 */
    pc_fsm_init(&f, false, 0);
    n = pc_fsm_on_ble(&f, PC_BLE_PAIR_FAIL, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_PAIR);

    /* ======== BLE 事件:断连回退(规格 §10) ======== */

    /* 演示模式断连 -> 待机主页(未全屏时零 effect)。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_ble(&f, PC_BLE_DISCONNECTED, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* 演示全屏中断连 -> 先退全屏再回待机。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    pc_fsm_on_action(&f, PC_ACT_FULLSCREEN_TOGGLE, true, fx, FX_CAP);
    n = pc_fsm_on_ble(&f, PC_BLE_DISCONNECTED, fx, FX_CAP);
    assert(n == 1);
    assert(fx[0].type == PC_FX_PAGE_EXIT_FULLSCREEN);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(!f.fullscreen);

    /* 媒体模式断连 -> 待机主页。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_MEDIA, true, fx, FX_CAP);
    assert(f.state == PC_ST_MEDIA);
    n = pc_fsm_on_ble(&f, PC_BLE_DISCONNECTED, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* 待机断连无转移。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_ble(&f, PC_BLE_DISCONNECTED, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    /* ======== 睡眠 / 唤醒:首键吞掉(规格 §6 转移表最后一行) ======== */

    /* 电源降级 -> SLEEP,置吞键标志、清全屏记忆。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    pc_fsm_on_action(&f, PC_ACT_FULLSCREEN_TOGGLE, true, fx, FX_CAP);
    n = pc_fsm_on_power(&f, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_SLEEP);
    assert(f.wake_key_pending);
    assert(!f.fullscreen);

    /* 首键(唤醒动作)被吞:零 effect,仅回待机。 */
    n = pc_fsm_on_action(&f, PC_ACT_WAKE, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(!f.wake_key_pending);

    /* 第二次按键正常出 effect(吞键只吞一次)。 */
    n = pc_fsm_on_action(&f, PC_ACT_MENU_OPEN, true, fx, FX_CAP);
    assert(f.state == PC_ST_STANDBY_MENU);

    /* 睡眠中直接吞任意动作(防御式,与动作类型无关)。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_power(&f, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_PAGE_NEXT, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(!f.wake_key_pending);

    /* 显式唤醒路径(非首键):回待机并清吞键标志。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_power(&f, true, fx, FX_CAP);
    n = pc_fsm_on_power(&f, false, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);
    assert(!f.wake_key_pending);

    /* ======== effect 截断行为 ======== */

    /* 进演示应产生 2 个 effect;cap=1 时只写 1 个,但返回值 = 2。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, 1);
    assert(n == 2);
    assert(fx[0].type == PC_FX_TIMER_RESET);

    /* cap=0 / out=NULL:仅返回应产生的总数。 */
    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, NULL, 0);
    assert(n == 2);

    /* PAGE_NEXT 应产生 2 个;cap=1 截断时返回值仍为 2。 */
    pc_fsm_init(&f, true, 0);
    pc_fsm_on_action(&f, PC_ACT_ENTER_PRESENT, true, fx, FX_CAP);
    n = pc_fsm_on_action(&f, PC_ACT_PAGE_NEXT, true, fx, 1);
    assert(n == 2);

    /* ======== 防御式:NULL 与无动作 ======== */
    assert(pc_fsm_on_action(NULL, PC_ACT_MENU_OPEN, true, fx, FX_CAP) == 0);
    assert(pc_fsm_on_ble(NULL, PC_BLE_CONNECTED, fx, FX_CAP) == 0);
    assert(pc_fsm_on_power(NULL, true, fx, FX_CAP) == 0);

    pc_fsm_init(&f, true, 0);
    n = pc_fsm_on_action(&f, PC_ACT_NONE, true, fx, FX_CAP);
    assert(n == 0);
    assert(f.state == PC_ST_STANDBY_HOME);

    return 0;
}
