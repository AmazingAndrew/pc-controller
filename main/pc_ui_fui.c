// main/pc_ui_fui.c
// FUI 主题核心实现:设计令牌、整屏骨架、面板/灯条/页脚/扫描线公共
// 构件、一次性黑场转场。契约见 pc_ui_fui.h。
//
// 渲染纪律(逐条对照 ui-design §5,全部构件与页面遵守):
//   - 进场一次性全屏绘制底纹(16 批 × 240×20),此后只刷脏区;
//   - 禁止每帧全屏动画;禁止双缓冲(单 240×20 行缓冲红线,
//     components/bsp/src/bsp_display_lvgl.c double_buffer = false);
//   - 灯条/闪烁半周期 >= 500 ms;扫描线仅 240×2 px 亮带
//     (脏区 960 B < 1 KB/帧);
//   - 页面转场为一次性黑场,无连续过渡动画。
//
// 字体占位声明(统一写法):
//   "Placeholder font; final HUD pixel font pending OFL license
//   verification (requirements §15)."
//
// 线程契约:全部函数假定调用方已持 bsp_lvgl_lock();内部不加锁。
#include "pc_ui_fui.h"

#include "esp_log.h"

#include "bsp_display.h"
#include "pc_storage.h"

/* ---- 文件内状态 ---- */

static lv_obj_t *s_scr;        /* 当前活动屏幕(转场期间为 NULL) */
static uint8_t s_backlight = 100; /* 用户背光档(转场后恢复用) */
static void (*s_leave_cb)(void);  /* 当前页登记的退页清理钩子 */

/* ESP_LOGE 标签:覆盖本页所有 lv_obj_create() 失败日志。 */
static const char *TAG = "pc_ui_fui";

/* 电池控件:挂屏顶栏右侧。电池读数 -1 时只画外框不画数字(§8)。 */
static lv_obj_t *s_batt_fill;  /* 外框内填充条(宽度随电量) */
static lv_obj_t *s_batt_pct;   /* 百分比标签("-1" 时清空隐藏) */

/* 黑场转场的降背光档:12% 足够看清过渡又不刺眼。 */
#define PC_FUI_DIM_PCT 12u

/* ---- 小助手 ---- */

/* 纯色矩形块:无圆角、无边框、零内边距、不可滚动。 */
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (o == NULL) return NULL;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

lv_obj_t *pc_fui_label(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    if (l == NULL) return NULL;
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* ---- 顶栏电池控件(§2/§3:右上角图标 + 百分比;-1 降级 §8) ---- */

static void battery_widget(lv_obj_t *scr)
{
    /* 百分比标签:图标左侧,荧光黄,8 px 占位字体。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    s_batt_pct = pc_fui_label(scr, "", &lv_font_unscii_8,
                              lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(s_batt_pct, LV_ALIGN_TOP_RIGHT, -38, 11);

    /* 电池外框:18×10,2 px FRAME 边,透明底。 */
    lv_obj_t *frame = lv_obj_create(scr);
    if (frame == NULL) {
        ESP_LOGE(TAG, "Failed to create battery frame (LVGL OOM?)");
        return;
    }
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(frame, 18, 10);
    lv_obj_align(frame, LV_ALIGN_TOP_RIGHT, -18, 9);
    lv_obj_set_style_radius(frame, 1, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(PC_FUI_C_FRAME), 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_pad_all(frame, 1, 0);

    /* 内部填充条:宽度随电量;初始 0 = 外框态。 */
    s_batt_fill = lv_obj_create(frame);
    if (s_batt_fill == NULL) {
        ESP_LOGE(TAG, "Failed to create battery fill (LVGL OOM?)");
        return;
    }
    lv_obj_remove_flag(s_batt_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_batt_fill, 0, 0);
    lv_obj_set_size(s_batt_fill, 0, 4);
    lv_obj_set_style_radius(s_batt_fill, 0, 0);
    lv_obj_set_style_border_width(s_batt_fill, 0, 0);
    lv_obj_set_style_pad_all(s_batt_fill, 0, 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(PC_FUI_C_STATUS), 0);

    /* 电池正极凸起:3×4 小块。 */
    (void)block(scr, 224, 12, 3, 4, PC_FUI_C_FRAME);
}

void pc_fui_set_battery(int percent)
{
    if (s_batt_fill == NULL || s_batt_pct == NULL) return;
    if (percent < 0) {
        /* §8 降级:读数不可用(-1)——只画电池外框,不画数字,
         * 绝不显示 "-1" 或 "0%"。 */
        lv_obj_set_width(s_batt_fill, 0);
        lv_label_set_text(s_batt_pct, "");
        return;
    }
    if (percent > 100) percent = 100;
    /* 外框内容区宽 = 18 - 2×2(边) - 2×1(pad) = 12 px。 */
    lv_obj_set_width(s_batt_fill, (12 * percent + 50) / 100);
    /* 满电用 OK_GREEN(稳定/充足语义的局部复用);其余荧光黄。 */
    lv_obj_set_style_bg_color(s_batt_fill,
        lv_color_hex(percent >= 95 ? PC_FUI_C_OK_GREEN : PC_FUI_C_STATUS), 0);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(s_batt_pct, buf);
}

/* ---- 整屏骨架 ---- */

/* 网格底纹:竖线每 24 px、横线每 40 px 一条 1 px FRAME 暗线。
 * 进场随整屏一次性绘制(§5 规则 1),此后静态不重绘。 */
static void grid_texture(lv_obj_t *scr)
{
    for (int x = 24; x < PC_FUI_W; x += 24) {
        (void)block(scr, x, 0, 1, PC_FUI_H, PC_FUI_C_FRAME);
    }
    for (int y = 40; y < PC_FUI_H; y += 40) {
        (void)block(scr, 0, y, PC_FUI_W, 1, PC_FUI_C_FRAME);
    }
}

lv_obj_t *pc_fui_screen_create(const char *mode)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    if (scr == NULL) return NULL;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(PC_FUI_C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);

    grid_texture(scr);

    /* 外框:1 px FRAME 包边(§2 令牌用途),用屏幕自身边框实现,
     * 零对象成本。 */
    lv_obj_set_style_border_color(scr, lv_color_hex(PC_FUI_C_FRAME), 0);
    lv_obj_set_style_border_width(scr, 1, 0);

    /* 顶栏模式名(左侧):青色 + 青色文字阴影模拟发光(§2 顶栏)。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *mode_label = pc_fui_label(scr, mode, &lv_font_montserrat_14,
                                        lv_color_hex(PC_FUI_C_LABEL));
    lv_obj_align(mode_label, LV_ALIGN_TOP_LEFT, PC_FUI_SAFE, 8);
    /* LVGL v9 移除 lv_obj_set_style_text_shadow_*；
     * 文本"发光"效果改走 label 的 box shadow:
     *   - shadow_color == 文字颜色 (青色) → 同色光晕
     *   - shadow_width == 原 text_shadow_width → 投影宽度
     *   - shadow_ofs_x/y 默认 0 → 中心对齐（与原 text_shadow 一致） */
    lv_obj_set_style_shadow_color(mode_label,
                                  lv_color_hex(PC_FUI_C_LABEL), 0);
    lv_obj_set_style_shadow_width(mode_label, 6, 0);
    lv_obj_set_style_shadow_opa(mode_label, LV_OPA_COVER, 0);

    battery_widget(scr);
    return scr;
}

lv_obj_t *pc_fui_screen(void)
{
    return s_scr;
}

/* ---- 面板(§2 构件规则) ---- */

lv_obj_t *pc_fui_panel_create(lv_obj_t *parent, int w, int h,
                              const char *label)
{
    lv_obj_t *p = lv_obj_create(parent);
    if (p == NULL) return NULL;
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(PC_FUI_C_PANEL_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);

    /* 2 px PANEL_GLOW 边框。 */
    lv_obj_set_style_border_color(p, lv_color_hex(PC_FUI_C_GLOW), 0);
    lv_obj_set_style_border_width(p, 2, 0);

    /* 内外发光用 lv_style shadow 模拟(单缓冲下阴影随面板一次性
     * 绘制,不产生持续脏区):
     *   外发光 = 10 px 宽 30% 透明橙影;
     *   内发光 = 4 px 宽 60% 透明橙影,spread 0 贴边内渗。 */
    lv_obj_set_style_shadow_color(p, lv_color_hex(PC_FUI_C_GLOW), 0);
    lv_obj_set_style_shadow_width(p, 10, 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_x(p, 0, 0);
    lv_obj_set_style_shadow_ofs_y(p, 0, 0);
    lv_obj_set_style_shadow_spread(p, 4, 0);

    /* 内容允许贴边(角标压在面板边角上)。 */
    lv_obj_add_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* 左上青色面板标签(§2 面板标签 ≈ 8 px,FRAME 旁注用青色)。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *l = pc_fui_label(p, label, &lv_font_unscii_8,
                               lv_color_hex(PC_FUI_C_LABEL));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 16, 7);

    /* 对角双角标:左上 + 右下,2 px 厚荧光黄括号(§2 构件规则:
     * diagonal pair, top-left and bottom-right)。 */
    (void)block(p, 2, 2, 10, 2, PC_FUI_C_STATUS); /* 左上横臂 */
    (void)block(p, 2, 2, 2, 10, PC_FUI_C_STATUS); /* 左上竖臂 */
    (void)block(p, w - 12, h - 4, 10, 2, PC_FUI_C_STATUS); /* 右下横臂 */
    (void)block(p, w - 4, h - 12, 2, 10, PC_FUI_C_STATUS); /* 右下竖臂 */
    return p;
}

/* ---- 灯条(§2:12×5 px 段;橙/青两种点亮语义) ---- */

lv_obj_t *pc_fui_lamp_create(lv_obj_t *parent, int x, int y)
{
    const int w = PC_FUI_LAMP_N * PC_FUI_LAMP_W +
                  (PC_FUI_LAMP_N - 1) * PC_FUI_LAMP_GAP;
    lv_obj_t *lamp = lv_obj_create(parent);
    if (lamp == NULL) return NULL;
    lv_obj_remove_flag(lamp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(lamp, x, y);
    lv_obj_set_size(lamp, w, PC_FUI_LAMP_H);
    lv_obj_set_style_radius(lamp, 0, 0);
    lv_obj_set_style_border_width(lamp, 0, 0);
    lv_obj_set_style_pad_all(lamp, 0, 0);
    lv_obj_set_style_bg_opa(lamp, LV_OPA_TRANSP, 0);

    for (int i = 0; i < PC_FUI_LAMP_N; i++) {
        (void)block(lamp, i * (PC_FUI_LAMP_W + PC_FUI_LAMP_GAP), 0,
                    PC_FUI_LAMP_W, PC_FUI_LAMP_H, PC_FUI_C_LAMP_OFF);
    }
    return lamp;
}

void pc_fui_lamp_paint(lv_obj_t *lamp, pc_fui_lamp_mode_t mode)
{
    if (lamp == NULL) return;
    uint32_t c = (mode == PC_FUI_LAMP_ORANGE) ? PC_FUI_C_GLOW :
                 (mode == PC_FUI_LAMP_CYAN)   ? PC_FUI_C_LABEL :
                                                PC_FUI_C_LAMP_OFF;
    const int n = (int)lv_obj_get_child_count(lamp);
    for (int i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(lamp, i),
                                  lv_color_hex(c), 0);
    }
}

/* ---- 底栏按键图例(§2/§3:1 px FRAME 分隔线 + 键名黄/动作白) ---- */

lv_obj_t *pc_fui_footer_create(lv_obj_t *scr,
                               const pc_fui_footer_entry_t *entries,
                               int count, lv_obj_t **out_actions)
{
    /* 分隔线:顶栏 296 处的 1 px FRAME 线(§3)。 */
    (void)block(scr, 0, PC_FUI_FOOTER_Y, PC_FUI_W, 1, PC_FUI_C_FRAME);

    lv_obj_t *row = lv_obj_create(scr);
    if (row == NULL) return NULL;
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(row, PC_FUI_SAFE, PC_FUI_FOOTER_Y + 2);
    lv_obj_set_size(row, PC_FUI_W - 2 * PC_FUI_SAFE, 22);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, 0);

    for (int i = 0; i < count && i < PC_FUI_FOOTER_MAX; i++) {
        lv_obj_t *item = lv_obj_create(row);
        if (item == NULL) continue;
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

        /* 键名:荧光黄;动作:文白。8 px 占位字体,图例两行内自动换行。
         * Placeholder font; final HUD pixel font pending OFL license
         * verification (requirements §15). */
        lv_obj_t *key = pc_fui_label(item, entries[i].key,
                                     &lv_font_unscii_8,
                                     lv_color_hex(PC_FUI_C_STATUS));
        lv_obj_set_width(key, LV_SIZE_CONTENT);
        lv_obj_t *act = pc_fui_label(item, entries[i].action,
                                     &lv_font_unscii_8,
                                     lv_color_hex(PC_FUI_C_TEXT));
        lv_obj_set_width(act, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(act, LV_TEXT_ALIGN_CENTER, 0);
        if (out_actions != NULL) out_actions[i] = act;
    }
    return row;
}

/* ---- 扫描线亮带(§7:240×2,2-4 s 周期) ---- */

static void scanline_set_y(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

void pc_fui_scanline_create(lv_obj_t *scr)
{
    lv_obj_t *band = lv_obj_create(scr);
    if (band == NULL) return;
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(band, 0, -2);
    lv_obj_set_size(band, PC_FUI_W, 2);
    lv_obj_set_style_radius(band, 0, 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(PC_FUI_C_TEXT), 0);
    /* 半透明亮带:低不透明度,只提亮扫过的窄条。 */
    lv_obj_set_style_bg_opa(band, LV_OPA_20, 0);

    /* 渲染纪律(§5 预算表):亮带脏区 = 240×2 px = 960 B @ RGB565,
     * < 1 KB/帧;周期 3 s(规格 2-4 s 区间),线性往复循环,
     * 不产生整屏撕裂窗口。 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, band);
    lv_anim_set_exec_cb(&a, scanline_set_y);
    lv_anim_set_values(&a, -2, PC_FUI_H);
    lv_anim_set_duration(&a, 3000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

/* ---- 页面生命周期:一次性黑场转场(§5 规则 4) ---- */

/* 退页清场(规格 §7 行 164 退出顺序:先停可能访问 UI 的动画/
 * 定时器,再删屏清指针)。页面自建的灯条定时器/动画由页面登记
 * 的退页钩子停掉;屏级动画(扫描线等)在此统一清除。 */
static void teardown_screen(void)
{
    if (s_scr == NULL) return;
    lv_anim_delete(NULL, NULL);    /* 全部动画(切页时仅本页有动画) */
    lv_obj_delete(s_scr);
    s_scr = NULL;
    s_batt_fill = NULL;
    s_batt_pct = NULL;
}

void pc_fui_set_leave_cb(void (*cb)(void))
{
    s_leave_cb = cb;
}

lv_obj_t *pc_fui_switch_page(pc_fui_builder_t build)
{
    /* 1-2. 页面退场钩子(停自建定时器/动画)→ 删旧屏 → 降背光。 */
    if (s_leave_cb != NULL) {
        s_leave_cb();
        s_leave_cb = NULL;
    }
    teardown_screen();
    bsp_display_backlight(PC_FUI_DIM_PCT);

    /* 3-4. 建新屏并载入。构建失败:保留一个最小降级屏(纯底色),
     * 保证屏幕始终有可显示对象(规格 §9 内存红线下的最后防线)。 */
    lv_obj_t *next = build();
    if (next == NULL) {
        next = lv_obj_create(NULL);
        if (next != NULL) {
            lv_obj_set_style_bg_color(next, lv_color_hex(PC_FUI_C_BG), 0);
            lv_obj_set_style_border_width(next, 0, 0);
        }
    }
    if (next == NULL) return NULL; /* LVGL 内存池彻底耗尽,放弃渲染 */
    s_scr = next;
    lv_screen_load(s_scr);

    /* 5. 恢复背光。说明:新屏的首帧渲染在释放 LVGL 锁后由渲染
     * 任务完成;背光恢复与之同拍,视觉上为一次快速黑场闪切,
     * 符合 §5 规则 4"无连续过渡动画"。 */
    bsp_display_backlight(s_backlight);
    return s_scr;
}

void pc_fui_init(void)
{
    /* 背光档取用户配置(失败回填默认 100,见 pc_storage §10)。
     * 0 档抬到 8,避免转场恢复后全灭不可见。 */
    pc_cfg_t cfg;
    (void)pc_cfg_load(&cfg);
    s_backlight = cfg.backlight;
    if (s_backlight < 8) s_backlight = 8;
}
