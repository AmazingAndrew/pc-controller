// main/pc_ui_fui_media.c
// FUI 媒体/动作反馈共用页 + 睡眠过渡页 —— 数据驱动,不含业务逻辑。
//
// 页面规格对照:ui-design §4.4 Action Feedback / Media:
//   两个子状态共用本页:
//   - 锁屏反馈:面板标签行 "COMMAND" / 组合键(随档案:
//     WIN+L / CTRL+CMD+Q / SUPER+L),大字主词(如 "LOCKED"),
//     副行 "PROFILE: 档案名"(WINDOWS / MACOS / LINUX)。
//     显示 1.5 s 后由组装层自动切回待机(定时器归组装层管)。
//   - 媒体模式:面板标签行 "MEDIA MODE" / "VOL nn"(两位音量);
//     底栏 "UP VOL+ / DOWN VOL- / OK PLAY";OK 长按回待机。
//   - 锁屏仅能从待机触发,反馈页也只在待机触发后出现(§4.4)。
//   非锁屏类反馈(配对成功/失败、槽清除、保存)复用反馈版式:
//   大字主词 + 可空副行。
//
// 睡眠过渡页(规格 §6):最简页面——深底 + 一个模式名标签,
// 无面板无动画,唤醒首键即离开。
//
// 渲染纪律(§5):反馈为一次性静态绘制(1.5 s 内无任何动画,
// 脏区为零);媒体页同为静态(音量事件驱动刷新);禁止每帧
// 全屏动画;禁止双缓冲。
//
// 线程契约:调用方已持 bsp_lvgl_lock();静态对象指针退页时清空。
#include <stddef.h>
#include <string.h>

#include "lvgl.h"

#include "pc_strings.h"
#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* ---- 文件内状态(媒体模式页) ---- */

static lv_obj_t *s_vol;       /* "VOL nn" 数值标签 */
static lv_obj_t *s_media_footer_acts[PC_FUI_FOOTER_MAX];

/* ---- 退页钩子 ---- */

static void media_leave(void)
{
    s_vol = NULL;
    for (int i = 0; i < PC_FUI_FOOTER_MAX; i++) {
        s_media_footer_acts[i] = NULL;
    }
    pc_fui_set_leave_cb(NULL);
}

/* ---- 媒体模式建屏(§4.4) ---- */

lv_obj_t *pc_ui_media_build(void)
{
    lv_obj_t *scr = pc_fui_screen_create("MEDIA");
    if (scr == NULL) return NULL;

    /* h1 "MEDIA MODE":荧光黄 -3° 倾斜。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *h1 = pc_fui_label(scr, "MEDIA MODE",
                                &lv_font_montserrat_20,
                                lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_pos(h1, 12, 44);
    lv_obj_set_style_transform_rotation(h1, -30, 0);
    lv_obj_set_style_transform_pivot_x(h1, 0, 0);
    lv_obj_set_style_transform_pivot_y(h1, 18, 0);
    lv_obj_set_style_clip_corner(h1, true, 0);
    lv_obj_set_style_transform_width(h1, 24, 0);

    lv_obj_t *sub = pc_fui_label(scr, "CONSUMER CONTROL",
                                 &lv_font_unscii_8,
                                 lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_set_pos(sub, 16, 72);
    lv_obj_set_style_text_letter_space(sub, 2, 0);

    /* ---- 面板 "MEDIA MODE":标签行值 "VOL nn" ---- */
    lv_obj_t *p = pc_fui_panel_create(scr, 220, 96, "MEDIA MODE");
    lv_obj_set_pos(p, PC_FUI_SAFE, 96);

    /* 音量两位数字(占位默认值来自缓存;音量推流接口
     * pc_ui_set_volume 是 M2 追加项,组装层接入前显示缓存值)。 */
    char vol_txt[8];
    lv_snprintf(vol_txt, sizeof(vol_txt), "VOL %02d", pc_ui_cache_volume());
    s_vol = pc_fui_label(p, vol_txt, &lv_font_montserrat_20,
                         lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_width(s_vol, 190);
    lv_obj_set_style_text_align(s_vol, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_vol, LV_ALIGN_TOP_MID, 0, 44);

    /* ---- 底栏:UP VOL+ / DOWN VOL- / OK PLAY ---- */
    const pc_fui_footer_entry_t entries[PC_FUI_FOOTER_MAX] = {
        { "UP", "VOL+" },
        { "DOWN", "VOL-" },
        { "OK", "PLAY" },
    };
    (void)pc_fui_footer_create(scr, entries, PC_FUI_FOOTER_MAX,
                               s_media_footer_acts);

    /* 扫描线最后创建(< 1 KB/帧)。 */
    pc_fui_scanline_create(scr);

    pc_fui_set_leave_cb(media_leave);
    return scr;
}

/* 音量刷新:仅媒体页在屏时生效(路由在 pc_ui.c 判定)。 */
void pc_ui_media_set_volume(int vol)
{
    if (s_vol == NULL) return;
    if (vol < 0) vol = 0;
    if (vol > 99) vol = 99;
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "VOL %02d", vol);
    lv_label_set_text(s_vol, buf);
}

void pc_ui_media_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}

/* ================================================================
 * 动作反馈版式(§4.4):1.5 s 短暂屏显,纯静态绘制。
 * ================================================================ */

/* 把组装层拼好的 detail("WIN+L / PROFILE: WINDOWS")拆成
 * 组合键与档案名两段;格式不符时按整串放组合键位。
 * 内存:只写调用方缓冲,不保留指针。 */
static void split_detail(const char *detail, char *combo, int combo_cap,
                         char *profile, int profile_cap)
{
    combo[0] = '\0';
    profile[0] = '\0';
    if (detail == NULL || detail[0] == '\0') return;

    const char *sep = strstr(detail, " / ");
    if (sep == NULL) {
        lv_snprintf(combo, (size_t)combo_cap, "%s", detail);
        return;
    }
    const size_t n = (size_t)(sep - detail);
    if ((int)n >= combo_cap) return;
    memcpy(combo, detail, n);
    combo[n] = '\0';

    const char *p = sep + 3; /* 跳过 " / " */
    if (strncmp(p, "PROFILE: ", 9) == 0) p += 9;
    lv_snprintf(profile, (size_t)profile_cap, "%s", p);
}

/* 反馈页构建:大字主词 + COMMAND/PROFILE 行(有组合键时)。
 * 由 pc_ui.c 在 show_feedback 时经黑场转场载入。 */
lv_obj_t *pc_ui_media_feedback_build(const char *title, const char *detail);

lv_obj_t *pc_ui_media_feedback_build(const char *title, const char *detail)
{
    lv_obj_t *scr = pc_fui_screen_create("PC-CTRL");
    if (scr == NULL) return NULL;

    char combo[24];
    char profile[16];
    split_detail(detail, combo, (int)sizeof(combo),
                 profile, (int)sizeof(profile));

    /* 面板:有组合键走锁屏版式(§4.4),否则通用反馈版式。 */
    const bool lock_style = (combo[0] != '\0');
    lv_obj_t *p = pc_fui_panel_create(scr, 220, 140,
                                      lock_style ? "COMMAND" : "SYSTEM");
    lv_obj_set_pos(p, PC_FUI_SAFE, 88);

    if (lock_style) {
        /* 标签行值:锁屏组合键(随档案)。 */
        lv_obj_t *cv = pc_fui_label(p, combo, &lv_font_montserrat_14,
                                    lv_color_hex(PC_FUI_C_STATUS));
        lv_obj_align(cv, LV_ALIGN_TOP_MID, 88, 12);
        (void)cv;
    }

    /* 大字主词(如 "LOCKED" / "PAIRED" / "SLOT CLEARED")。 */
    lv_obj_t *big = pc_fui_label(p, title != NULL ? title : "",
                                 &lv_font_montserrat_20,
                                 lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_width(big, 200);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(big, LV_ALIGN_TOP_MID, 0, lock_style ? 50 : 56);
    lv_obj_set_style_text_shadow_color(big,
                                       lv_color_hex(PC_FUI_C_GLOW), 0);
    lv_obj_set_style_text_shadow_width(big, 8, 0);

    /* 副行:档案名(锁屏)或通用副行文案。 */
    if (lock_style && profile[0] != '\0') {
        char line[28];
        lv_snprintf(line, sizeof(line), "PROFILE: %s", profile);
        lv_obj_t *pl = pc_fui_label(p, line, &lv_font_unscii_8,
                                    lv_color_hex(PC_FUI_C_TEXT));
        lv_obj_align(pl, LV_ALIGN_TOP_MID, 0, 100);
        (void)pl;
    } else if (detail != NULL && detail[0] != '\0' && !lock_style) {
        lv_obj_t *dl = pc_fui_label(p, detail, &lv_font_unscii_8,
                                    lv_color_hex(PC_FUI_C_TEXT));
        lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 100);
        (void)dl;
    }

    /* 反馈页无底栏图例(1.5 s 即自动返回,无交互)。
     * 也不放扫描线动画:纯静态零脏区,符合 §5。 */
    return scr;
}

/* ================================================================
 * 睡眠过渡页(规格 §6):最简页面。
 * ================================================================ */

lv_obj_t *pc_ui_sleep_build(void)
{
    /* 模式名走字符串表;不建面板/灯条/扫描线,唤醒即走。 */
    lv_obj_t *scr = pc_fui_screen_create(pc_str_en[PC_STR_MODE_SLEEP]);
    if (scr == NULL) return NULL;

    lv_obj_t *l = pc_fui_label(scr, pc_str_en[PC_STR_MODE_SLEEP],
                               &lv_font_montserrat_20,
                               lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_center(l);
    return scr;
}

void pc_ui_sleep_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}
