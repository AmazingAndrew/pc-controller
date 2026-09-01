// main/pc_ui_fui_standby.c
// FUI 待机主页(开机/主页)—— 数据驱动页面,不含业务逻辑。
//
// 页面规格对照:ui-design §4.1 Standby (boot/home):
//   - 顶栏 "PC-CTRL"(青色发光)+ 电池图标/百分比;
//   - h1 "STANDBY"(荧光黄,约 -3° 倾斜)+ 副标题
//     "PRESENTATION CORE V1.0"(FRAME 暗色宽字距);
//   - 面板 1 "HOST LINK":标签行状态字(搜索中/已连接/已断开)+
//     大字状态(启动期 "PLEASE WAIT",连接后主机名)+ 灯条;
//   - 面板 2 "PROFILE":标签行,值 = 当前槽位档案
//     (WINDOWS / MACOS / LINUX,荧光黄,右侧);
//   - 底栏:OK: PAIR/PRESENT(随连接态改词)+ UP: MENU +
//     DOWN: SLOT + HOLD OK: LOCK(文案走字符串表);
//     C1 修复:底栏补 "DOWN SLOT" 图例(规格 ui-design §4.1:
//     OK PAIR/PRESENT / UP MENU / DOWN SLOT);HOLD OK: LOCK
//     移到“轻提示文字”或下一行,本页只保留三组键名图例。
//   - 状态变体:搜索中(灯条渐进填充动画)/ 已连接(灯条常亮)/
//     已断开(灯条熄灭)。
//
// 渲染纪律(§5):
//   - 底纹随整屏进场一次性绘制,此后仅脏区刷新;
//   - 灯条渐进填充以 500 ms 为最小步进(§7:半周期 >= 500 ms),
//     每步只改 1 个 12×5 px 段色,脏区 120 B;
//   - 禁止每帧全屏动画;禁止双缓冲。
//
// 线程契约:调用方已持 bsp_lvgl_lock();静态对象指针退页时清空。
#include <stddef.h>
#include <string.h>

#include "lvgl.h"

#include "pc_host_profiles.h"
#include "pc_storage.h"
#include "pc_strings.h"
#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* ---- 文件内对象指针(退页清空) ---- */

static lv_obj_t *s_status_word; /* 标签行状态字(黄) */
static lv_obj_t *s_big;         /* 大字状态(启动词/主机名) */
static lv_obj_t *s_lamp;        /* 灯条容器 */
static lv_obj_t *s_footer_acts[PC_FUI_FOOTER_MAX]; /* 底栏动作标签 */

/* 灯条段指针缓存(渐进填充动画逐段点亮用)。 */
static lv_obj_t *s_segs[PC_FUI_LAMP_N];

/* 搜索灯条动画状态:单次动画值 0..1000 线性推进,回调按阈值
 * 逐段点亮 = "渐进填充"(§7);时长 3000 ms / 6 段 = 每段 500 ms
 * 步进,满足灯条半周期 >= 500 ms 红线。 */
static lv_anim_t s_fill_anim;

/* ---- 退页钩子:停动画、清静态指针 ---- */

static void standby_leave(void)
{
    lv_anim_delete(s_lamp, NULL);
    s_status_word = NULL;
    s_big = NULL;
    s_lamp = NULL;
    for (int i = 0; i < PC_FUI_FOOTER_MAX; i++) s_footer_acts[i] = NULL;
    for (int i = 0; i < PC_FUI_LAMP_N; i++) s_segs[i] = NULL;
    pc_fui_set_leave_cb(NULL);
}

/* ---- 灯条渐进填充动画 ---- */

static void lamp_fill_cb(void *arg, int32_t v)
{
    (void)arg;
    /* v 0..1000 线性:每 1000/6 点亮一段;只重绘跨阈值的那段,
     * 单段脏区 = 12×5×2 B = 120 B。 */
    const int lit = (int)(((int32_t)v * PC_FUI_LAMP_N) / 1000);
    for (int i = 0; i < PC_FUI_LAMP_N; i++) {
        const uint32_t c = (i < lit) ? PC_FUI_C_GLOW : PC_FUI_C_LAMP_OFF;
        lv_obj_set_style_bg_color(s_segs[i], lv_color_hex(c), 0);
    }
}

static void start_searching_anim(void)
{
    lv_anim_init(&s_fill_anim);
    lv_anim_set_var(&s_fill_anim, s_lamp);
    lv_anim_set_exec_cb(&s_fill_anim, lamp_fill_cb);
    lv_anim_set_values(&s_fill_anim, 0, 1000);
    lv_anim_set_duration(&s_fill_anim, 3000);
    lv_anim_set_repeat_count(&s_fill_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&s_fill_anim, lv_anim_path_linear);
    lv_anim_start(&s_fill_anim);
}

/* 链路三态刷新(§4.1 state variants):
 *   连接   -> 状态字 "CONNECTED",大字 = 主机名,灯条常亮;
 *   未连接 -> 状态字 "DISCONNECTED"(任务 #51:稳态呈现,
 *             不再以"SEARCHING"循环灯条动画诱导用户等待),
 *             大字 "PLEASE WAIT",灯条熄灭。
 *
 * 任务 #51 设计决策:
 *   - 旧版以 "SEARCHING" + 灯条渐进填充动画表示"正在回连/正在配对",
 *     暗示设备持续尝试连接;但连接建立与否取决于主机侧(进入配对模式
 *     后等待主机发起),设备侧在等待期间一直 "搜索" 与现实不符;
 *   - 新版改为稳态 "DISCONNECTED":灯条熄灭, 大字保留
 *     "PLEASE WAIT" 提示用户此时去主机侧操作;
 *   - 何时重新 "搜索" 动画 = 何时进入配对模式 (有向/通用广播打开),
 *     那是配对页 (PC_ST_PAIR) 的职责,不在 STANDBY 页里表现。
 *   - 进入/退出配对模式由组装层推 (状态机吐 PC_FX_START_PAIR /
 *     PC_FX_STOP_PAIR),与本页"状态字刷新"独立。 */
static void apply_link(bool connected, const char *host)
{
    if (s_status_word == NULL) return;

    /* 任务 #51: 不论连接与否,先停掉旧动画,避免"闪烁动画残留"
     * 跨状态泄漏。PC_FUI_LAMP_OFF 是稳态终态。 */
    lv_anim_delete(s_lamp, NULL);
    if (connected) {
        lv_label_set_text(s_status_word,
                          pc_str_en[PC_STR_STATUS_CONNECTED]);
        lv_label_set_text(s_big,
                          (host != NULL && host[0] != '\0') ? host : "HOST");
        pc_fui_lamp_paint(s_lamp, PC_FUI_LAMP_ORANGE); /* 常亮 */
    } else {
        /* 任务 #51: 稳态 DISCONNECTED, 不起搜索动画, 灯条熄灭。 */
        lv_label_set_text(s_status_word,
                          pc_str_en[PC_STR_STATUS_DISCONNECTED]);
        lv_label_set_text(s_big, "PLEASE WAIT");
        pc_fui_lamp_paint(s_lamp, PC_FUI_LAMP_OFF);
    }

    /* 底栏 OK 动作词:已连接 = 进演示,未连接 = 进配对(图例走
     * 字符串表)。仅改一个标签文本 = 局部脏区。 */
    if (s_footer_acts[0] != NULL) {
        lv_label_set_text(s_footer_acts[0],
            pc_str_en[connected ? PC_STR_HINT_PRESENT : PC_STR_HINT_PAIR]);
    }
}

/* ---- 建屏 ---- */

lv_obj_t *pc_ui_standby_build(void)
{
    lv_obj_t *scr = pc_fui_screen_create("PC-CTRL");
    if (scr == NULL) return NULL;

    /* h1 "STANDBY":荧光黄,约 -3° 倾斜(§2 页面标题规则)。
     * Placeholder font; final HUD pixel font pending OFL license
     * verification (requirements §15). */
    lv_obj_t *h1 = pc_fui_label(scr, "STANDBY", &lv_font_montserrat_20,
                                lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_set_pos(h1, 14, 38);
    lv_obj_set_style_transform_rotation(h1, -30, 0);   /* 0.1° 单位 */
    lv_obj_set_style_transform_pivot_x(h1, 0, 0);
    lv_obj_set_style_transform_pivot_y(h1, 18, 0);
    lv_obj_set_style_clip_corner(h1, true, 0);
    lv_obj_set_style_transform_width(h1, 24, 0);

    /* 副标题:FRAME 暗色、宽字距(§2)。 */
    lv_obj_t *sub = pc_fui_label(scr, "PRESENTATION CORE V1.0",
                                 &lv_font_unscii_8,
                                 lv_color_hex(PC_FUI_C_FRAME));
    lv_obj_set_pos(sub, 16, 66);
    lv_obj_set_style_text_letter_space(sub, 2, 0);

    /* ---- 面板 1 "HOST LINK"(内容区 32-296,10 px 安全边距) ---- */
    lv_obj_t *p1 = pc_fui_panel_create(scr, 220, 104, "HOST LINK");
    lv_obj_set_pos(p1, PC_FUI_SAFE, 82);

    s_status_word = pc_fui_label(p1, "SEARCHING", &lv_font_unscii_8,
                                 lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(s_status_word, LV_ALIGN_TOP_LEFT, 16, 24);

    /* 大字状态:20 px 占位(规格 20-24 px;最终像素字体见 §6)。 */
    s_big = pc_fui_label(p1, "PLEASE WAIT", &lv_font_montserrat_20,
                         lv_color_hex(PC_FUI_C_TEXT));
    lv_obj_set_width(s_big, 190);
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, 44);

    /* 灯条:6 段 12×5,面板内水平居中。 */
    s_lamp = pc_fui_lamp_create(p1, (220 - 82) / 2, 84);
    for (int i = 0; i < PC_FUI_LAMP_N; i++) {
        s_segs[i] = lv_obj_get_child(s_lamp, i);
    }

    /* ---- 面板 2 "PROFILE":标签行,值右对齐荧光黄(§4.1) ---- */
    lv_obj_t *p2 = pc_fui_panel_create(scr, 220, 44, "PROFILE");
    lv_obj_set_pos(p2, PC_FUI_SAFE, 196);

    /* 档案值 = 当前槽位记录的 OS 档案;读 NVS 失败时槽载入自动
     * 回填默认值(0 = WINDOWS),页面不防御存储故障。 */
    pc_cfg_t cfg;
    (void)pc_cfg_load(&cfg);
    pc_slot_t slot;
    (void)pc_slot_load(cfg.slot, &slot);
    const char *profile = pc_profile_name((pc_os_t)slot.os);
    lv_obj_t *prof = pc_fui_label(p2, profile != NULL ? profile : "?",
                                  &lv_font_montserrat_14,
                                  lv_color_hex(PC_FUI_C_STATUS));
    lv_obj_align(prof, LV_ALIGN_TOP_MID, 0, 22);
    (void)prof;

    /* ---- 底栏按键图例(文案走字符串表) ---- */
    char host[17];
    bool linked = pc_ui_cache_link(host, (int)sizeof(host));
    const pc_fui_footer_entry_t entries[PC_FUI_FOOTER_MAX] = {
        /* OK 动作词随连接态:已连接进演示 / 未连接进配对。
         * 组装层在本页显示期间不调 pc_ui_set_link(断连即回本页
         * 重走状态切换),故无需在屏期间再改词;仍保留改词能力。
         * 注:表中图例文案自带键名前缀(如 "OK: PAIR"),故键名
         * 列置空,整串作为动作列显示,避免 "OK OK: PAIR" 重复。
         *
         * C1 修复:三组图例按规格 ui-design §4.1 调整为
         *   OK PAIR/PRESENT / UP MENU / DOWN SLOT。
         * 原 HOLD OK: LOCK 提示在 PC_FUI_FOOTER_MAX=3 的页脚上限下
         * 不能再保留;锁屏交互可由待机菜单第 3 项后接提示词
         * 引导(本实现不影响功能路径)。 */
        { "", pc_str_en[linked ? PC_STR_HINT_PRESENT : PC_STR_HINT_PAIR] },
        { "", pc_str_en[PC_STR_HINT_MENU] },
        { "", "DOWN: SLOT" },
    };
    lv_obj_t *footer = pc_fui_footer_create(scr, entries,
                                            PC_FUI_FOOTER_MAX,
                                            s_footer_acts);
    (void)footer;

    /* 扫描线亮带必须最后创建(覆盖全部构件;脏区 < 1 KB/帧)。 */
    pc_fui_scanline_create(scr);

    /* 按缓存还原链路现场,随后登记退页钩子。 */
    apply_link(linked, host);
    pc_fui_set_leave_cb(standby_leave);
    return scr;
}

/* ---- 局部刷新入口(仅对应对象重绘,不重建整页) ---- */

void pc_ui_standby_set_link(bool connected, const char *host)
{
    if (s_status_word == NULL) return;
    apply_link(connected, host);
}

void pc_ui_standby_set_battery(int percent)
{
    pc_fui_set_battery(percent);
}
