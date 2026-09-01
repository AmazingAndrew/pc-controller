// main/pc_app_fsm.c
// 应用状态机实现。所有转移均逐行对应规格
// (docs/software-design/pc-controller/requirements.md)§6 转移表与键位
// 矩阵,修改前请先对照规格行 104-133。
//
// 平台无关:仅依赖 C11 标准库与两个纯逻辑头文件,可被 host 测试
// 直接编译(范式见 tests/test_ui_pixel_math.c)。
#include "pc_app_fsm.h"

#include <string.h>

#include "pc_hid_reports.h" /* PC_KEY_* 键码常量(HID 翻页/全屏键) */

/* 待机菜单项数量。规格 §6(行 135)8 项 + #42 新增第 9 项 RESET BLE +
 * #46 新增第 10 项 SCREENSHOT,顺序固定:
 *   0 PAIRING、1 CLEAR SLOT、2 SLOT、3 HOST PROFILE、
 *   4 KEY SOUND、5 BACKLIGHT、6 MEDIA MODE、7 ABOUT、
 *   8 RESET BLE(两步式确认,本里程碑新增)、
 *   9 SCREENSHOT(任务 #46,串口截屏触发)。 */
#define PC_MENU_COUNT 10

/* 设备槽位数量。规格 §1:三个设备槽位,串行切换
 * (受 MAX_CONNECTIONS=1 约束)。 */
#define PC_SLOT_TOTAL 3

/* effect 输出缓冲的内部游标结构。
 * n 记录"应产生"的总数(可能超过 cap),写入量受 cap 限制,
 * 使调用方可以用返回值判断截断(接口约定见头文件)。 */
typedef struct {
    pc_effect_t *out; /* 输出缓冲,可为 NULL */
    int cap;          /* 缓冲容量 */
    int n;            /* 应产生的 effect 总数 */
} pc_fx_buf_t;

/* 追加一个 effect。先整体清零联合体(避免残值跨成员泄漏),
 * 再由调用方按 type 填参。超出 cap 时只计数不写入。 */
static void fx_push(pc_fx_buf_t *b, pc_fx_t type)
{
    if (b->n < b->cap && b->out != NULL) {
        pc_effect_t e;
        memset(&e, 0, sizeof(e));
        e.type = type;
        b->out[b->n] = e;
    }
    b->n++;
}

/* 追加一个带参数的 effect。set 回调在"已写入"的元素上填参;
 * 被截断(未写入)的元素不填,避免空指针写。 */
static void fx_push_arg(pc_fx_buf_t *b, pc_fx_t type, void (*set)(pc_effect_t *e, uintptr_t v), uintptr_t v)
{
    int idx = b->n;
    fx_push(b, type);
    if (idx < b->cap && b->out != NULL && set != NULL) {
        set(&b->out[idx], v);
    }
}

/* 各参数填充回调:把 uintptr_t 还原为对应联合体成员。 */
static void fx_set_key(pc_effect_t *e, uintptr_t v)
{
    /* 高 8 位放键码,低 8 位放修饰位(打包约定见调用处)。 */
    e->arg.key.mods = (uint8_t)(v & 0xFFU);
    e->arg.key.keycode = (uint8_t)((v >> 8) & 0xFFU);
}

static void fx_set_usage(pc_effect_t *e, uintptr_t v)
{
    e->arg.usage = (uint16_t)v;
}

static void fx_set_slot(pc_effect_t *e, uintptr_t v)
{
    e->arg.slot = (uint8_t)v;
}

static void fx_set_delta(pc_effect_t *e, uintptr_t v)
{
    e->arg.page_delta = (int)(intptr_t)v;
}

/* 便捷封装:发一帧键盘报告 effect(修饰位 + 键码)。 */
static void fx_hid_key(pc_fx_buf_t *b, uint8_t mods, uint8_t keycode)
{
    fx_push_arg(b, PC_FX_HID_KEY, fx_set_key, ((uintptr_t)keycode << 8) | mods);
}

/* 便捷封装:发一帧 Consumer 报告 effect(16 位 usage)。 */
static void fx_consumer(pc_fx_buf_t *b, uint16_t usage)
{
    fx_push_arg(b, PC_FX_HID_CONSUMER, fx_set_usage, usage);
}

/* 便捷封装:槽位相关 effect(切换/清除)。 */
static void fx_slot(pc_fx_buf_t *b, pc_fx_t type, uint8_t slot)
{
    fx_push_arg(b, type, fx_set_slot, slot);
}

/* 便捷封装:页码步进 effect。delta 取 ±1。 */
static void fx_step(pc_fx_buf_t *b, int delta)
{
    fx_push_arg(b, PC_FX_PAGE_STEP, fx_set_delta, (uintptr_t)(intptr_t)delta);
}

/* 进入待机主页(所有"回待机"转移的公共收口)。
 * 依据规格 §6 转移表:菜单长按退出、演示/媒体长按退出、配对取消、
 * 配对成功、断连回退,终点都是 STANDBY (home)。 */
static void enter_standby_home(pc_fsm_t *f)
{
    f->state = PC_ST_STANDBY_HOME;
    f->menu_sel = 0; /* 离开菜单页时复位选中项,下次打开从头开始 */
}

/* 进入配对模式:规格 §6 转移表
 *   "STANDBY (menu) | Menu item PAIRING confirmed | PAIR"
 * 以及待机主页未连接时的直达路径(键位矩阵行 3)。 */
static void enter_pair(pc_fsm_t *f, pc_fx_buf_t *b)
{
    f->state = PC_ST_PAIR;
    f->menu_sel = 0;
    fx_push(b, PC_FX_START_PAIR);
}

/* 菜单确认分发。仅在待机菜单页调用。
 * 9 项行为逐一对照规格 §6 行 135 与任务契约(#42 新增第 9 项):
 *   0 PAIRING      -> 进配对(转移表行 5)
 *   1 CLEAR SLOT   -> 清当前槽绑定,停留菜单
 *   2 SLOT         -> 循环切换槽位,停留菜单
 *   3 HOST PROFILE -> 配置类,持久化,停留菜单
 *   4 KEY SOUND    -> 配置类,持久化,停留菜单
 *   5 BACKLIGHT    -> 配置类,持久化,停留菜单
 *   6 MEDIA MODE   -> 进媒体模式(转移表行 4)
 *   7 ABOUT        -> 仅屏显,无 effect
 *   8 RESET BLE    -> #42 两步式重置(具体逻辑在组装层处理武装/
 *                    执行,状态机仅吐 0 effect 让业务层按菜单项
 *                    索引判别)。 */
static void menu_confirm(pc_fsm_t *f, pc_fx_buf_t *b)
{
    switch (f->menu_sel) {
    case 0: /* PAIRING */
        enter_pair(f, b);
        break;
    case 1: /* CLEAR SLOT */
        fx_slot(b, PC_FX_SLOT_CLEAR, f->slot);
        break;
    case 2: /* SLOT:与待机主页 DOWN 同语义(1->2->3->1,规格 §6 行 123) */
        f->slot = (uint8_t)((f->slot + 1U) % f->slot_count);
        fx_slot(b, PC_FX_SLOT_SWITCH, f->slot);
        break;
    case 3: /* HOST PROFILE */
    case 4: /* KEY SOUND */
    case 5: /* BACKLIGHT */
        fx_push(b, PC_FX_SAVE_CFG);
        break;
    case 6: /* MEDIA MODE:转移表行 4 */
        f->state = PC_ST_MEDIA;
        f->menu_sel = 0;
        fx_push(b, PC_FX_ENTER_MEDIA);
        break;
    case 7: /* ABOUT:仅屏显,状态机不产生任何 effect */
    case 8: /* #42 RESET BLE:两步式重置的武装/执行状态机在组装层,
             * 状态机不吐 effect(on_menu_confirm_apply 在调用
             * pc_fsm_on_action 之后根据 menu_sel == 8 自行处理
             * 武装/执行)。 */
    case 9: /* #46 SCREENSHOT:状态机不吐 effect,组装层在
             * on_menu_confirm_apply 中按 menu_sel == 9 触发
             * pc_screenshot_capture(),本档保持零 effect 让业务层
             * 按菜单项索引判别即可。 */
    default:
        break;
    }
}

void pc_fsm_init(pc_fsm_t *f, bool any_bond, uint8_t initial_slot)
{
    if (f == NULL) {
        return;
    }
    /* 无绑定 -> 配对模式(规格 §1/FR-07:无绑定首次上电自动进配对);
     * 有绑定 -> 待机主页。 */
    f->state = any_bond ? PC_ST_STANDBY_HOME : PC_ST_PAIR;
    f->fullscreen = false;
    f->menu_sel = 0;
    f->slot = (uint8_t)(initial_slot % PC_SLOT_TOTAL); /* 防御越界配置 */
    f->slot_count = PC_SLOT_TOTAL;
    f->wake_key_pending = false;
    f->any_bond = any_bond;
}

int pc_fsm_on_action(pc_fsm_t *f, pc_action_t a, bool ble_connected, pc_effect_t *out, int cap)
{
    pc_fx_buf_t b = { out, cap, 0 };

    if (f == NULL) {
        return 0;
    }

    /* 唤醒吞键(规格 §6 转移表最后一行:
     * "SLEEP | Any key | STANDBY (first key event consumed, no function)")。
     * 睡眠后的第一个动作只清标志并回待机,不产生任何 effect,
     * 且与动作类型无关(键位矩阵对 SLEEP 只会给出 WAKE,但防御式
     * 处理任意动作)。 */
    if (f->wake_key_pending) {
        f->wake_key_pending = false;
        enter_standby_home(f);
        return 0;
    }

    switch (a) {
    case PC_ACT_NONE:
        /* 无动作:不转移、不产生 effect。 */
        break;

    case PC_ACT_WAKE:
        /* 显式唤醒动作(幂等):非睡眠态收到时同样回待机。 */
        enter_standby_home(f);
        break;

    case PC_ACT_MENU_OPEN:
        /* 待机主页 -> 菜单页(转移表行 1:"STANDBY (home) | UP short | MENU")。 */
        if (f->state == PC_ST_STANDBY_HOME) {
            f->state = PC_ST_STANDBY_MENU;
            f->menu_sel = 0;
        }
        break;

    case PC_ACT_MENU_NEXT:
        /* 菜单下一项,8 项回绕(0..7)。回绕逻辑归属本模块(规格 §6)。 */
        if (f->state == PC_ST_STANDBY_MENU) {
            f->menu_sel = (uint8_t)((f->menu_sel + 1U) % PC_MENU_COUNT);
        }
        break;

    case PC_ACT_MENU_PREV:
        /* 菜单上一项,+7 取模实现回绕。 */
        if (f->state == PC_ST_STANDBY_MENU) {
            f->menu_sel = (uint8_t)((f->menu_sel + PC_MENU_COUNT - 1U) % PC_MENU_COUNT);
        }
        break;

    case PC_ACT_MENU_CONFIRM:
        /* OK 单击:确认所选菜单项(键位矩阵行 5)。 */
        if (f->state == PC_ST_STANDBY_MENU) {
            menu_confirm(f, &b);
        }
        break;

    case PC_ACT_MENU_EXIT:
        /* OK 长按:菜单 -> 待机主页(转移表行 6:"MENU | OK long | STANDBY")。 */
        if (f->state == PC_ST_STANDBY_MENU) {
            enter_standby_home(f);
        }
        break;

    case PC_ACT_ENTER_PRESENT:
        /* 待机主页(已连接) -> 演示模式(转移表行 2)。守卫:必须已连接,
         * 未连接时键位矩阵给的是进配对,这里二次防御拒绝。
         * 进入动作产生两个 effect:
         *   TIMER_RESET          —— 演讲计时器清零(规格 §1/FR-05);
         *   PAGE_ENTER_FULLSCREEN —— 页码来源复位到 1(规格 §1/FR-12:
         *   "进入全屏时从 1 开始计数";进入演示即视为进入演示全屏
         *   语义场景)。注意:此处不发送 F5 键——主机侧全屏由用户
         *   第一次 OK 单击触发(FR-02:第一次 OK 短按进全屏),
         *   全屏记忆位从"未全屏"开始,保证 F5/Esc 交替的第一次是
         *   进全屏,不与进入 effect 重复。 */
        if (f->state == PC_ST_STANDBY_HOME && ble_connected) {
            f->state = PC_ST_PRESENT;
            f->menu_sel = 0;
            f->fullscreen = false;
            fx_push(&b, PC_FX_TIMER_RESET);
            fx_push(&b, PC_FX_PAGE_ENTER_FULLSCREEN);
        }
        break;

    case PC_ACT_ENTER_PAIR:
        /* 待机主页(未连接) -> 配对模式(转移表行 3)。 */
        if (f->state == PC_ST_STANDBY_HOME) {
            enter_pair(f, &b);
        }
        break;

    case PC_ACT_ENTER_MEDIA:
        /* 媒体模式的正规入口是菜单第 7 项(转移表行 4);动作层同样
         * 允许组装层直接驱动(例如未来的快捷入口)。 */
        if (f->state == PC_ST_STANDBY_HOME || f->state == PC_ST_STANDBY_MENU) {
            f->state = PC_ST_MEDIA;
            f->menu_sel = 0;
            fx_push(&b, PC_FX_ENTER_MEDIA);
        }
        break;

    case PC_ACT_PAGE_NEXT:
        /* 演示模式翻页:发 HID 键码 + 页码步进。
         * 方向约定(规格 §1/§6):UP = 下一页,发送方向键"上"
         * (PC_KEY_UP 0x52)——主流演示软件(如 PowerPoint)的方向键
         * "上"即前进一页;步进 +1。 */
        if (f->state == PC_ST_PRESENT) {
            fx_hid_key(&b, 0U, PC_KEY_UP);
            fx_step(&b, 1);
        }
        break;

    case PC_ACT_PAGE_PREV:
        /* DOWN = 上一页,发送方向键"下"(PC_KEY_DOWN 0x51),步进 -1。 */
        if (f->state == PC_ST_PRESENT) {
            fx_hid_key(&b, 0U, PC_KEY_DOWN);
            fx_step(&b, -1);
        }
        break;

    case PC_ACT_FULLSCREEN_TOGGLE:
        /* 全屏切换:记忆式翻转(规格 §1/FR-02)。
         * 第一次(记忆位 = 未全屏) -> 发跨平台三连发(#57:
         *   F5 / Cmd+Shift+Enter / Option+Cmd+P) + 进全屏;
         * 第二次(记忆位 = 全屏)   -> 发 Esc + 退全屏。
         * #57 跨平台兼容:原 F5 单发在 macOS Keynote/PowerPoint
         * 上不进全屏;三连发覆盖三个 OS 路径,各帧间留 200 ms 由
         * 组装层按"连续 HID_KEY effect"检测插入,状态机仅描述意图。
         * 设备记住翻转状态,保证在三个 OS 上行为一致。 */
        if (f->state == PC_ST_PRESENT) {
            if (!f->fullscreen) {
                /* Win: F5 */
                fx_hid_key(&b, 0U, PC_KEY_F5);
                /* macOS PowerPoint: Cmd + Shift + Enter */
                fx_hid_key(&b,
                           (uint8_t)(PC_MOD_LGUI | PC_MOD_LSHIFT),
                           PC_KEY_RETURN);
                /* macOS Keynote: Cmd + Option + P */
                fx_hid_key(&b,
                           (uint8_t)(PC_MOD_LGUI | PC_MOD_LALT),
                           PC_KEY_P);
                fx_push(&b, PC_FX_PAGE_ENTER_FULLSCREEN);
                f->fullscreen = true;
            } else {
                fx_hid_key(&b, 0U, PC_KEY_ESC);
                fx_push(&b, PC_FX_PAGE_EXIT_FULLSCREEN);
                f->fullscreen = false;
            }
        }
        break;

    case PC_ACT_TIMER_TOGGLE:
        /* 任务 #47: 演示模式 OK 长按切换计时暂停/恢复。
         * 状态机本身不持有"暂停位", 暂停状态由 pc_speech_timer_t
         * (组装层持有) 跟踪; 状态机仅吐一个语义 effect, 让组装层
         * 调 pc_speech_set_paused() 翻转暂停位并同步 UI 的
         * "PAUSED" 指示。
         * 该 action 不修改状态机状态, 不影响全屏记忆位 (timer 与
         * fullscreen 正交); 退出 PRESENT 仍由断连或配对路径触发
         * (规格 §6)。 */
        if (f->state == PC_ST_PRESENT) {
            fx_push(&b, PC_FX_TIMER_TOGGLE);
        }
        break;

    case PC_ACT_LOCK:
        /* 一键锁屏(规格 §1/FR-03):仅待机主页可直达(键位矩阵行 3)。
         *
         * 设计决策(锁屏 effect 方案):状态机不存储"每槽位主机配置",
         * 主机配置存放在 NVS 并由组装层持有(规格 §8),因此状态机
         * 只吐出一个语义 effect:
         *   PC_FX_SHOW_FEEDBACK_LOCK
         * 组装层收到后:
         *   1. 用 pc_lock_combo(当前槽位记录的 OS) 解析组合键;
         *   2. 用 pc_kbd_add() 组帧发送锁屏组合键,再补空报告;
         *   3. 给屏显/蜂鸣反馈。
         * 该方案把"组合键数据"(数据驱动表,规格 §11 可扩展)与
         * "状态转移"分离,状态机保持零配置依赖,可完整 host 测试。 */
        if (f->state == PC_ST_STANDBY_HOME) {
            fx_push(&b, PC_FX_SHOW_FEEDBACK_LOCK);
        }
        break;

    case PC_ACT_VOL_UP:
        /* 媒体模式音量 +(规格 §1/FR-04,Consumer Page)。 */
        if (f->state == PC_ST_MEDIA) {
            fx_consumer(&b, PC_USAGE_VOL_UP);
        }
        break;

    case PC_ACT_VOL_DOWN:
        /* 媒体模式音量 -。 */
        if (f->state == PC_ST_MEDIA) {
            fx_consumer(&b, PC_USAGE_VOL_DOWN);
        }
        break;

    case PC_ACT_PLAY_PAUSE:
        /* 媒体模式播放/暂停。 */
        if (f->state == PC_ST_MEDIA) {
            fx_consumer(&b, PC_USAGE_PLAY_PAUSE);
        }
        break;

    case PC_ACT_SLOT_NEXT:
        /* 循环切换槽位(1->2->3->1)。两处入口同语义:
         * 待机主页 DOWN(键位矩阵行 2)与配对模式 DOWN(行 10)。
         * 规格 §1/FR-06:切换先优雅断开当前连接,再对目标槽定向广播;
         * 断开 + 定向广播由组装层解释 PC_FX_SLOT_SWITCH 执行。 */
        if (f->state == PC_ST_STANDBY_HOME || f->state == PC_ST_PAIR) {
            f->slot = (uint8_t)((f->slot + 1U) % f->slot_count);
            fx_slot(&b, PC_FX_SLOT_SWITCH, f->slot);
        }
        break;

    case PC_ACT_CANCEL:
        /* 配对取消(转移表行 9:"PAIR | OK short (cancel) | STANDBY")。 */
        if (f->state == PC_ST_PAIR) {
            fx_push(&b, PC_FX_STOP_PAIR);
            enter_standby_home(f);
        }
        break;

    case PC_ACT_EXIT_TO_STANDBY:
        /* 演示/媒体长按退出(转移表行 7/8)。若演示中处于全屏,
         * 先发 Esc 键(规格 §1/FR-02)再吐"退全屏" effect,让
         * 主机退出全屏;与 PC_ACT_FULLSCREEN_TOGGLE 的退全屏路径
         * 对齐,避免主机停留在演示全屏(回归项 B1 修复)。 */
        if (f->state == PC_ST_PRESENT || f->state == PC_ST_MEDIA) {
            if (f->fullscreen) {
                fx_hid_key(&b, 0U, PC_KEY_ESC);
                fx_push(&b, PC_FX_PAGE_EXIT_FULLSCREEN);
                f->fullscreen = false;
            }
            enter_standby_home(f);
        }
        break;

    default:
        /* 越界动作值:防御式忽略。 */
        break;
    }

    return b.n;
}

int pc_fsm_on_ble(pc_fsm_t *f, pc_ble_evt_t ev, pc_effect_t *out, int cap)
{
    pc_fx_buf_t b = { out, cap, 0 };

    if (f == NULL) {
        return 0;
    }

    switch (ev) {
    case PC_BLE_CONNECTED:
        /* 连接建立:状态机无转移(连接状态由组装层持有并按需传入
         * pc_fsm_on_action)。保留该分支使事件枚举处理完备。 */
        break;

    case PC_BLE_DISCONNECTED:
        /* 断连回退:演示/媒体模式依赖活动连接,回到待机主页
         * (键位语义在断连后也由组装层按未连接路由)。
         * 若演示中处于全屏,先退全屏再回待机。
         * 断连后的重连广播序列(定向 30 s -> 通用 2 min,规格 §10)
         * 由组装层自行启动,不在本状态机吐 effect,避免与
         * 槽位切换/配对路径的广播控制重复。 */
        if (f->state == PC_ST_PRESENT || f->state == PC_ST_MEDIA) {
            if (f->fullscreen) {
                fx_push(&b, PC_FX_PAGE_EXIT_FULLSCREEN);
                f->fullscreen = false;
            }
            enter_standby_home(f);
        }
        break;

    case PC_BLE_PAIR_OK:
        /* 配对成功(规格 §1/FR-07):配对模式 -> 待机主页,
         * 更新 any_bond(此后上电不再自动进配对)。
         * 吐两个 effect:停止配对广播 + 持久化新绑定元数据
         * (槽位记录:绑定地址/主机名/OS 类型等,规格 §8)。 */
        if (f->state == PC_ST_PAIR) {
            f->any_bond = true;
            fx_push(&b, PC_FX_STOP_PAIR);
            fx_push(&b, PC_FX_SAVE_CFG);
            enter_standby_home(f);
        }
        break;

    case PC_BLE_PAIR_FAIL:
        /* 配对失败:停留在配对模式,屏显失败反馈由组装层完成;
         * 状态机不产生 effect。 */
        break;

    default:
        /* 越界事件值:防御式忽略。 */
        break;
    }

    return b.n;
}

int pc_fsm_on_power(pc_fsm_t *f, bool to_sleep, pc_effect_t *out, int cap)
{
    pc_fx_buf_t b = { out, cap, 0 };

    if (f == NULL) {
        return 0;
    }

    if (to_sleep) {
        /* 电源降级到睡眠(规格 §6 转移表行 10:
         * "Any active mode | Power timeout | SLEEP"):
         * 应用态进入 SLEEP,置唤醒吞键标志;清除全屏记忆
         * (睡眠后演示场景已中断,唤醒回待机)。
         * 定时器停启、背光与连接的睡眠策略由组装层执行,
         * 状态机不吐 effect。 */
        f->state = PC_ST_SLEEP;
        f->wake_key_pending = true;
        f->fullscreen = false;
        f->menu_sel = 0;
    } else {
        /* 显式唤醒(非"首键"路径,例如独立硬件电源键,规格 §10 行 195):
         * 直接回待机主页并清吞键标志。 */
        if (f->state == PC_ST_SLEEP) {
            f->wake_key_pending = false;
            enter_standby_home(f);
        }
    }

    return b.n;
}
