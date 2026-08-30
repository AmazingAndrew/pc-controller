// main/pc_ui_fui_pair.c
// FUI 配对页 —— 数据驱动页面,不含业务逻辑。
//
// 页面规格对照:ui-design §4.3 Pairing:
//   - 顶栏 "PAIRING" + 电池;
//   - h1 "BLE HID";副标题 "DISCOVERABLE . SLOT n/3"
//     (字符集约束:以 "." 替代规格示例的 "·");
//   - 面板 1 "HOST LINK":标签行值 "OPEN";中心词 "SEARCHING"
//     (16 px 规范值,占位用现有最大字号 20);副行
//     'SELECT "PC-CTRL" ON HOST'(字符集约束:引号不在子集内,
//     屏显写作 SELECT PC-CTRL ON HOST);灯条橙/青交替闪烁;
//   - 面板 2 "BOUND SLOTS":标签行,值 "x/3";
//   - 底栏:OK: CANCEL(表文案)+ DOWN: SLOT。
//   - 状态变体:广播中 = 灯条交替闪烁;连接进入 = 灯条落定为
//     待机风格(随后组装层切回待机页,本页无需自绘)。
//   - Passkey 模式(规格 §1/FR-07):复用本屏显示 6 位数字大字,
//     替换中心词位置。
//
// 渲染纪律(§5/§7):
//   - 灯条交替闪烁半周期 600 ms(>= 500 ms 红线),每拍只改
//     6 个 12×5 px 段色(脏区 360 B);
//   - 禁止每帧全屏动画;禁止双缓冲。
//
// 线程契约:调用方已持 bsp_lvgl_lock();静态对象指针退页时清空。
#include <stddef.h>
#include <stdio.h>

#include "lvgl.h"

#include "pc_storage.h"
#include "pc_strings.h"
#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* 灯条交替半周期:600 ms(§7:blink cadence >= 500 ms)。 */
#define PAIR_BLINK_HALF_MS 600

/* ---- 文件内状态 ---- */

static lv_obj_t *s_big;        /* 中心词 / 6 位配对码大字 */
static lv_obj_t *s_sub;        /* 副行提示 */
static lv_obj_t *s_lamp;       /* 灯条容器 */
static lv_obj_t *s_slot_lbl;   /* 副标题 "DISCOVERABLE . SLOT n/3"(C2 修复:
                                * 装在静态指针上以便切槽后只重绘该行,
                                * 不重建整页——遵堡 §5 脏区纪律)。 */
static lv_timer_t *s_blink;    /* 橙青交替定时器 */
static bool s_phase;           /* 交替相位 */

/* ---- 退页钩子 ---- */

static void pair_leave(void)
{
    if (s_blink != NULL) {
        lv_timer_delete(s_blink);
        s_blink = NULL;
    }
    s_big = NULL;
    s_sub = NULL;
    s_lamp = NULL;
    s_slot_lbl = NULL;
    pc_fui_set_leave_cb(NULL);
}

/* ---- 灯条橙青交替(§4.3:alternating orange/cyan) ---- */

static void blink_cb(lv_timer_t *t)
{
    (void)t;
    if (s_lamp == NULL) return;
    s_phase = !s_phase;
    pc_fui_lamp_paint(s_lamp,
        s_phase ? PC_FUI_LAMP_ORANGE : PC_FUI_LAMP_CYAN);
}

/* ---- 建屏 ---- */

lv_obj_t *pc_ui_pair_build(void)
{
    lv_obj_t *scr = pc_fui_screen_create("PAIRING");
    if (scr == NULL) return NULL;

    /* 当前槽位(读 NVS;失败回填默认槽 0,页面不防御存储故障)。 */
    pc_cfg_t cfg;
    (void)pc_cfg_load(&cfg);
    const int slot = (int)cfg.slot + 1; /* 屏显 1 基 */

    /* 已绑定槽计数(面板 2 值 "x/3")。 */
    int bound = 0;
    for (int i = 0; i < PC_SLOT_COUNT; i++) {
        pc_slot_t s;
        if (pc_slot_load((uint8_t)i, &s) == ESP_OK && s.bound) bound++;
    }

    /* h1 "BLE HID":荧光黄 -3° 倾斜。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *h1 = pc_fui_label(scr, "BLE HID", &lv_font_montserrat_20,
                                lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_pos(h1, 14, 38);
    lv_obj_set_style_transform_rotation(h1, -30, 0);
    lv_obj_set_style_transform_pivot_x(h1, 0, 0);
    lv_obj_set_style_transform_pivot_y(h1, 18, 0);
    lv_obj_set_style_clip_corner(h1, true, 0);
    lv_obj_set_style_transform_width(h1, 24, 0);

    /* 副标题 "DISCOVERABLE . SLOT n/3"(FRAME 暗色宽字距)。 */
    char sub_title[40];
    lv_snprintf(sub_title, sizeof(sub_title),
                "DISCOVERABLE . SLOT %d/3", slot);
    s_slot_lbl = pc_fui_label(scr, sub_title, &lv_font_unscii_8,
                              lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_set_pos(s_slot_lbl, 16, 66);
    lv_obj_set_style_text_letter_space(s_slot_lbl, 1, 0);

    /* ---- 面板 1 "HOST LINK"(配对广播态) ---- */
    lv_obj_t *p1 = pc_fui_panel_create(scr, 220, 120, "HOST LINK");
    lv_obj_set_pos(p1, PC_FUI_SAFE, 82);

    /* 标签行值 "OPEN"(右区,荧光黄)。 */
    lv_obj_t *open = pc_fui_label(p1, "OPEN", &lv_font_unscii_8,
                                  lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(open, LV_ALIGN_TOP_MID, 90, 7);
    (void)open;

    /* 中心词 "SEARCHING"(16 px 规范值,占位用现有最大字号 20)。
     * Passkey 到达时被 6 位数字替换(见 pc_ui_pair_show_passkey)。 */
    s_big = pc_fui_label(p1, "SEARCHING", &lv_font_montserrat_20,
                         lv_color_hex(PC_FUI_C_LABEL));
    lv_obj_set_width(s_big, 200);
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, 34);

    /* 副行:字符集约束,屏显不带引号。 */
    s_sub = pc_fui_label(p1, "SELECT PC-CTRL ON HOST", &lv_font_unscii_8,
                         lv_color_hex(PC_FUI_C_TEXT));
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 68);

    /* 灯条:橙青交替闪烁,600 ms 半周期。 */
    s_lamp = pc_fui_lamp_create(p1, (220 - 82) / 2, 92);
    s_phase = false;
    pc_fui_lamp_paint(s_lamp, PC_FUI_LAMP_ORANGE);
    s_blink = lv_timer_create(blink_cb, PAIR_BLINK_HALF_MS, NULL);

    /* ---- 面板 2 "BOUND SLOTS"(标签行,值 "x/3") ---- */
    lv_obj_t *p2 = pc_fui_panel_create(scr, 220, 40, "BOUND SLOTS");
    lv_obj_set_pos(p2, PC_FUI_SAFE, 212);
    char bound_txt[8];
    lv_snprintf(bound_txt, sizeof(bound_txt), "%d/3", bound);
    lv_obj_t *bv = pc_fui_label(p2, bound_txt, &lv_font_montserrat_14,
                                lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(bv, LV_ALIGN_TOP_MID, 88, 12);
    (void)bv;

    /* ---- 底栏:OK: CANCEL(表文案)+ DOWN: SLOT ---- */
    const pc_fui_footer_entry_t entries[2] = {
        { "", pc_str_en[PC_STR_HINT_CANCEL] },
        { "DOWN", "SLOT" },
    };
    (void)pc_fui_footer_create(scr, entries, 2, NULL);

    /* 扫描线最后创建(覆盖全部构件;< 1 KB/帧)。 */
    pc_fui_scanline_create(scr);

    pc_fui_set_leave_cb(pair_leave);
    return scr;
}

/* ---- Passkey 模式:复用本屏显示 6 位配对码(规格 §1/FR-07) ---- */

void pc_ui_pair_show_passkey(uint32_t code)
{
    if (s_big == NULL) return; /* 非配对页在屏:忽略(降级) */
    char buf[16];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)code);
    lv_label_set_text(s_big, buf); /* 大字显示 6 位数字 */
    lv_obj_set_style_text_color(s_big,
                                lv_color_hex(PC_FUI_C_STATUS), 0);
    if (s_sub != NULL) {
        lv_label_set_text(s_sub, "ENTER CODE ON HOST");
    }
    /* 灯条保持交替闪烁(广播未停)。 */
}

/* ---- 局部刷新入口 ---- */

void pc_ui_pair_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}

/* C2 修复:仅重绘副标题 "DISCOVERABLE . SLOT n/3"。装配层在
 * 处理 PC_FX_SLOT_SWITCH 且配对页在屏时调用;不重建整页
 * ——遵堡 ui-design §5 脏区纪律(只动该行文本,脏区 ~ 30×8 px)。
 * 装配层负责判断"配对页是否在屏",本页接口不防御该判。 */
void pc_ui_pair_set_slot(int n)
{
    if (s_slot_lbl == NULL) return; /* 非配对页在屏:忽略(降级) */
    if (n < 1) n = 1;
    if (n > PC_SLOT_COUNT) n = PC_SLOT_COUNT;
    char sub_title[40];
    lv_snprintf(sub_title, sizeof(sub_title),
                "DISCOVERABLE . SLOT %d/3", n);
    lv_label_set_text(s_slot_lbl, sub_title);
}
