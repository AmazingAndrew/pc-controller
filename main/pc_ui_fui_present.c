// main/pc_ui_fui_present.c
// FUI 演示核心页 —— 数据驱动页面,不含业务逻辑。
//
// 页面规格对照:ui-design §4.2 Present (core page):
//   - 顶栏 "PRESENT" + 电池;
//   - h1 "PRESENT MODE";副标题 "SLIDE CONTROL ACTIVE";
//   - 面板 1 "SPEECH TIMER":标签行右侧 "RUN" 状态词;大号青色
//     发光数字为页面主视觉。规格规范值为 36 px 像素字体:
//     >>> 36px 像素字体待许可核验后替换,见 requirements §15;
//     占位期以现有最大字号 (Montserrat 20) 呈现 <<<
//   - 面板 2 "SLIDE":页码大字(规范值 16 px,占位同上)+ 提示行
//     "UP . DOWN"(本地估算:主机不上报总页数,故永远不画
//     "x/y" 格式,也不画 "EST");
//   - 面板 3 "HOST LINK":单标签行,值 "STABLE"(荧光黄;此处也
//     是唯一允许用 OK_GREEN 表示稳定链路的位置,§4.2);
//   - 底栏:仅 "OK FULLSCR" 与 "OK HOLD EXIT" 两组(HOLD 对应
//     >= 800 ms 长按阈值,规格 §1/FR-03);双击槽位在事件词汇表
//     中保留但无绑定动作,图例相应留空(规格非目标)。
//   - 状态变体:断链时面板 3 切红色 "LOST" 文本 + 慢速闪烁
//     (告警红为状态局部色,令牌集外);锁屏/音量控制在本模式
//     禁用且不出现在图例。
//
// 渲染纪律(§5,逐条成文):
//   - 每秒计时刷新只改数字标签文本 = 只重绘数字矩形,
//     脏区预算 1-4 KB/s(§5 预算表 Timer digits 行);
//   - 页码仅在翻页事件时刷新,无周期重绘;
//   - LOST 闪烁半周期 700 ms(>= 500 ms 红线),只动标签矩形;
//   - 禁止每帧全屏动画;禁止双缓冲。
//
// 线程契约:调用方已持 bsp_lvgl_lock();静态对象指针退页时清空。
#include <stddef.h>

#include "lvgl.h"

#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* ---- 文件内对象指针(退页清空) ---- */

static lv_obj_t *s_timer;    /* 计时数字标签 */
static lv_obj_t *s_slide;    /* 页码标签 */
static lv_obj_t *s_link_val; /* 面板 3 链路值标签 */
static lv_timer_t *s_lost_blink; /* LOST 闪烁定时器(断链时起) */
static bool s_blink_phase;   /* 闪烁相位 */

/* ---- 退页钩子 ---- */

static void present_leave(void)
{
    if (s_lost_blink != NULL) {
        lv_timer_delete(s_lost_blink);
        s_lost_blink = NULL;
    }
    s_timer = NULL;
    s_slide = NULL;
    s_link_val = NULL;
    pc_fui_set_leave_cb(NULL);
}

/* ---- LOST 闪烁(§4.2/§7:慢速,只重绘标签矩形) ---- */

static void lost_blink_cb(lv_timer_t *t)
{
    (void)t;
    if (s_link_val == NULL) return;
    s_blink_phase = !s_blink_phase;
    /* 明暗两相:告警红 <-> 面板底色(近似消隐),脏区 = 标签矩形。 */
    lv_obj_set_style_text_color(s_link_val,
        lv_color_hex(s_blink_phase ? PC_FUI_C_ALERT : PC_FUI_C_PANEL_BG), 0);
}

/* ---- 链路状态刷新 ---- */

static void apply_link(bool connected)
{
    if (s_link_val == NULL) return;
    if (s_lost_blink != NULL) {
        lv_timer_delete(s_lost_blink);
        s_lost_blink = NULL;
    }
    if (connected) {
        /* STABLE:此处允许荧光黄或 OK_GREEN(§4.2:唯一可用
         * OK_GREEN 表示稳定链路之处),取荧光黄保持行内一致。 */
        lv_label_set_text(s_link_val, "STABLE");
        lv_obj_set_style_text_color(s_link_val,
                                    lv_color_hex(PC_FUI_C_STATUS), 0);
    } else {
        /* LOST:告警红(令牌集外状态局部色)+ 700 ms 半周期闪烁。 */
        lv_label_set_text(s_link_val, "LOST");
        lv_obj_set_style_text_color(s_link_val,
                                    lv_color_hex(PC_FUI_C_ALERT), 0);
        s_blink_phase = false;
        s_lost_blink = lv_timer_create(lost_blink_cb, 700, NULL);
    }
}

/* ---- 建屏 ---- */

lv_obj_t *pc_ui_present_build(void)
{
    lv_obj_t *scr = pc_fui_screen_create("PRESENT");
    if (scr == NULL) return NULL;

    /* h1 "PRESENT MODE":荧光黄 -3° 倾斜(§2 页面标题规则)。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *h1 = pc_fui_label(scr, "PRESENT MODE",
                                &lv_font_montserrat_20,
                                lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_pos(h1, 12, 36);
    lv_obj_set_style_transform_rotation(h1, -30, 0);
    lv_obj_set_style_transform_pivot_x(h1, 0, 0);
    lv_obj_set_style_transform_pivot_y(h1, 18, 0);
    lv_obj_set_style_clip_corner(h1, true, 0);
    lv_obj_set_style_transform_width(h1, 24, 0);

    /* 副标题:FRAME 暗色宽字距。 */
    lv_obj_t *sub = pc_fui_label(scr, "SLIDE CONTROL ACTIVE",
                                 &lv_font_unscii_8,
                                 lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_set_pos(sub, 16, 64);
    lv_obj_set_style_text_letter_space(sub, 2, 0);

    /* ---- 面板 1 "SPEECH TIMER"(页面主视觉) ---- */
    lv_obj_t *p1 = pc_fui_panel_create(scr, 220, 84, "SPEECH TIMER");
    lv_obj_set_pos(p1, PC_FUI_SAFE, 80);

    /* 标签行右侧状态词 "RUN"(青色)。 */
    lv_obj_t *run = pc_fui_label(p1, "RUN", &lv_font_unscii_8,
                                 lv_color_hex(PC_FUI_C_LABEL));
    lv_obj_align(run, LV_ALIGN_TOP_MID, 88, 7);
    (void)run;

    /* 计时数字:青色发光大字,水平居中。
     * >>> 36px 像素字体待许可核验后替换,见 requirements §15;
     * 占位期用现有最大字号 20。每秒仅本标签重绘(脏区 1-4 KB/s)。 */
    s_timer = pc_fui_label(p1, pc_ui_cache_timer(),
                           &lv_font_montserrat_20,
                           lv_color_hex(PC_FUI_C_LABEL));
    lv_obj_set_width(s_timer, 190);
    lv_obj_set_style_text_align(s_timer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_timer, LV_ALIGN_TOP_MID, 0, 44);
    /* LVGL v9 移除 lv_obj_set_style_text_shadow_*；改走 box shadow。 */
    lv_obj_set_style_shadow_color(s_timer,
                                  lv_color_hex(PC_FUI_C_LABEL), 0);
    lv_obj_set_style_shadow_width(s_timer, 8, 0);
    lv_obj_set_style_shadow_opa(s_timer, LV_OPA_COVER, 0);

    /* ---- 面板 2 "SLIDE"(16 px 规范值,占位用现有最大字号) ---- */
    lv_obj_t *p2 = pc_fui_panel_create(scr, 220, 72, "SLIDE");
    lv_obj_set_pos(p2, PC_FUI_SAFE, 172);

    /* 页码:本地估算(规格 §1/FR-12);主机永不上报总页数,
     * 故不画 "x/y" 也不画 "EST";page < 1 时显示占位符 "-"。 */
    char num[8];
    const int page = pc_ui_cache_slide();
    if (page >= 1) {
        lv_snprintf(num, sizeof(num), "%d", page);
    } else {
        lv_snprintf(num, sizeof(num), "-");
    }
    s_slide = pc_fui_label(p2, num, &lv_font_montserrat_20,
                           lv_color_hex(PC_FUI_C_GLOW)); /* 橙色发光页码 */
    lv_obj_set_width(s_slide, 190);
    lv_obj_set_style_text_align(s_slide, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_slide, LV_ALIGN_TOP_MID, 0, 22);

    /* 提示行:字符集约束(无箭头符号)写作 "UP . DOWN"。 */
    lv_obj_t *hint = pc_fui_label(p2, "UP . DOWN", &lv_font_unscii_8,
                                  lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 54);
    (void)hint;

    /* ---- 面板 3 "HOST LINK"(单标签行) ---- */
    lv_obj_t *p3 = pc_fui_panel_create(scr, 220, 40, "HOST LINK");
    lv_obj_set_pos(p3, PC_FUI_SAFE, 252);
    s_link_val = pc_fui_label(p3, "STABLE", &lv_font_unscii_8,
                              lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(s_link_val, LV_ALIGN_TOP_MID, 88, 7);

    /* ---- 底栏:仅两组图例;HOLD = >= 800 ms 长按(规格 §1/
     * FR-03)。双击槽位保留无绑定,图例留空(规格非目标)。 ---- */
    const pc_fui_footer_entry_t entries[2] = {
        { "OK", "FULLSCR" },
        { "OK", "HOLD EXIT" },
    };
    (void)pc_fui_footer_create(scr, entries, 2, NULL);

    /* 扫描线最后创建(覆盖全部构件;< 1 KB/帧)。 */
    pc_fui_scanline_create(scr);

    /* 按缓存还原现场:链路与页码;计时文本建屏时已取缓存。 */
    char host[17];
    apply_link(pc_ui_cache_link(host, (int)sizeof(host)));

    pc_fui_set_leave_cb(present_leave);
    return scr;
}

/* ---- 局部刷新入口 ---- */

/* 每秒计时:只改数字标签文本(脏区预算 1-4 KB/s,§5 预算表)。 */
void pc_ui_present_set_timer(const char *text)
{
    if (s_timer == NULL || text == NULL) return;
    lv_label_set_text(s_timer, text);
}

/* 页码:仅翻页事件驱动,无周期重绘(§5 预算表)。 */
void pc_ui_present_set_slide(int page)
{
    if (s_slide == NULL) return;
    char num[8];
    if (page >= 1) {
        lv_snprintf(num, sizeof(num), "%d", page);
    } else {
        lv_snprintf(num, sizeof(num), "-");
    }
    lv_label_set_text(s_slide, num);
}

void pc_ui_present_set_link(bool connected, const char *host)
{
    (void)host; /* 本页链路行只显示稳定词,不显示主机名 */
    apply_link(connected);
}

void pc_ui_present_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}
