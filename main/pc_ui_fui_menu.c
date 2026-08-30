// main/pc_ui_fui_menu.c
// FUI 待机菜单页 —— 8 项列表,数据驱动,不含业务逻辑。
//
// 页面规格对照:ui-design §4.1(menu 段):
//   - 待机主页经 UP 键进入的独立 8 项列表页;
//   - 选中项高亮 = 橙底黑字(橙 = PANEL_GLOW);
//   - OK 短按进入/确认所选;OK 长按返回待机主页(交互语义属
//     组装层/状态机,本页只负责呈现选中项)。
// 菜单 8 项与顺序(规格 §6 行 135,与字符串表逐条对应):
//   0 PAIRING、1 CLEAR SLOT、2 SLOT、3 HOST PROFILE、
//   4 KEY SOUND、5 BACKLIGHT、6 MEDIA MODE、7 ABOUT。
//
// 选中项数据通路:选中索引由组装层状态机持有
// (pc_fsm_t.menu_sel);组装层在按键处理后经
// pc_ui_set_menu_sel() 推给本页(该接口为 M2 在 pc_ui.h 的追加
// 项,既有 8 接口签名未动)。本页自身仅做"收到索引 -> 局部重绘
// 新旧两行高亮",不解释菜单业务。
//
// 渲染纪律(§5):
//   - 底纹随整屏进场一次性绘制;
//   - 选中项移动只重绘新旧两行(2×(220×27) ≈ 11.6 KB 一次性,
//     无周期刷新);禁止整列表重建。
//
// 线程契约:调用方已持 bsp_lvgl_lock();静态对象指针退页时清空。
#include <stddef.h>

#include "lvgl.h"

#include "pc_strings.h"
#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* ---- 布局常量 ---- */

#define MENU_ITEMS  8     /* 菜单项数(规格 §6 行 135) */
#define MENU_ROW_H  27    /* 行高:8×27 = 216,恰好铺满内容区 */
#define MENU_TOP    40    /* 首行 y(顶栏 32 之下留缝) */
#define MENU_LEFT   10    /* 10 px 安全边距 */
#define MENU_W      220   /* 240 - 2×10 */

/* 8 项文案:直接取字符串表,索引与枚举一一对应(表项顺序即
 * 菜单顺序,见 pc_strings.h 注释)。 */
static const pc_str_t s_items[MENU_ITEMS] = {
    PC_STR_MENU_PAIRING,
    PC_STR_MENU_CLEAR_SLOT,
    PC_STR_MENU_SLOT,
    PC_STR_MENU_HOST_PROFILE,
    PC_STR_MENU_KEY_SOUND,
    PC_STR_MENU_BACKLIGHT,
    PC_STR_MENU_MEDIA_MODE,
    PC_STR_MENU_ABOUT,
};

/* ---- 文件内状态 ---- */

static lv_obj_t *s_rows[MENU_ITEMS];  /* 行容器(退页清空) */
static lv_obj_t *s_texts[MENU_ITEMS]; /* 行文本(退页清空) */
static int s_sel;                     /* 当前选中项(值跨转场保留,
                                       * 反馈页返回后维持原选中) */

/* ---- 高亮刷新:橙底黑字(选中) / 面板底白字(未选中) ---- */

static void paint_row(int i, bool selected)
{
    if (s_rows[i] == NULL) return;
    lv_obj_set_style_bg_color(s_rows[i],
        lv_color_hex(selected ? PC_FUI_C_GLOW : PC_FUI_C_PANEL_BG), 0);
    lv_obj_set_style_border_color(s_rows[i],
        lv_color_hex(selected ? PC_FUI_C_GLOW : PC_FUI_C_FRAME), 0);
    lv_obj_set_style_text_color(s_texts[i],
        lv_color_hex(selected ? 0x000000u : PC_FUI_C_TEXT), 0);
}

/* ---- 退页钩子 ---- */

static void menu_leave(void)
{
    for (int i = 0; i < MENU_ITEMS; i++) {
        s_rows[i] = NULL;
        s_texts[i] = NULL;
    }
    pc_fui_set_leave_cb(NULL);
}

/* ---- 建屏 ---- */

lv_obj_t *pc_ui_menu_build(void)
{
    lv_obj_t *scr = pc_fui_screen_create("PC-CTRL");
    if (scr == NULL) return NULL;

    /* 页面标题:菜单无独立模式名,顶栏已由骨架构建;内容区直接
     * 铺 8 行列表。行容器 = 面板底 + 1 px 边框;选中项换橙底黑字。 */
    for (int i = 0; i < MENU_ITEMS; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        if (row == NULL) continue;
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(row, MENU_LEFT, MENU_TOP + i * MENU_ROW_H);
        lv_obj_set_size(row, MENU_W, MENU_ROW_H - 2);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);

        /* Placeholder font; final HUD pixel font pending OFL license
         * verification (requirements §15). */
        lv_obj_t *t = pc_fui_label(row, pc_str_en[s_items[i]],
                                   &lv_font_montserrat_14,
                                   lv_color_hex(PC_FUI_C_TEXT));
        lv_obj_center(t);

        s_rows[i] = row;
        s_texts[i] = t;
        paint_row(i, i == s_sel);
    }

    /* 列表底部补一条底栏分隔线 + 图例:确认/返回(长按)语义。
     * 表中 HINT_BACK 自带 "HOLD OK:" 前缀,键名列置空。 */
    const pc_fui_footer_entry_t entries[1] = {
        { "", pc_str_en[PC_STR_HINT_BACK] },
    };
    (void)pc_fui_footer_create(scr, entries, 1, NULL);

    /* 扫描线最后创建(覆盖全部构件;< 1 KB/帧)。 */
    pc_fui_scanline_create(scr);

    pc_fui_set_leave_cb(menu_leave);
    return scr;
}

/* ---- 局部刷新入口 ---- */

/* 选中项移动:仅重绘新旧两行(渲染纪律:不重建整页)。 */
void pc_ui_menu_set_sel(int sel)
{
    if (sel < 0 || sel >= MENU_ITEMS) sel = 0; /* 越界按 0(防御式) */
    if (sel == s_sel) return;
    const int old = s_sel;
    s_sel = sel;
    paint_row(old, false);
    paint_row(sel, true);
}

/* 从非菜单态进入菜单时选中项归零(与状态机 menu_sel 复位同步,
 * 见 pc_app_fsm.c enter_standby_home / MENU_OPEN)。 */
void pc_ui_menu_reset_sel(void)
{
    s_sel = 0;
}

void pc_ui_menu_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}
