// main/pc_ui.c
// PC Controller UI 接口实现层:满足 pc_ui.h 全部接口(原 8 项 +
// M2 追加 2 项),把调用路由到当前活动页面的刷新函数。
//
// 设计(对照任务契约与 ui-design):
//   - 维护"当前页"状态,与组装层应用状态 (pc_state_t) 一一对应;
//     反馈页作为临时覆盖页单独跟踪(模式态不变,1.5 s 后组装层
//     调 pc_ui_show_state 恢复原模式页,ui-design §4.4/§7);
//   - 页面切换统一走 pc_fui_switch_page 的一次性黑场转场
//     (§5 规则 4:降背光 -> 删旧屏 -> 建新屏 -> 恢复背光);
//   - 电量/连接/计时/页码的 set 接口只局部刷新对应对象,
//     不重建整页(§5 脏区纪律);
//   - 各读数在本地缓存,页面重建时按缓存还原现场。
//
// 线程契约(规格 §7 行 159、ui-design §8):
//   全部接口假定调用方已持 bsp_lvgl_lock()(组装层在非 LVGL
//   任务中已统一加锁);本层与页面层均不二次加锁。显示初始化
//   失败时组装层不会创建/调用本模块(无屏降级,规格 §10)。
//
// 页面退出顺序(规格 §7 行 164):先停可能访问 UI 的定时器/动画,
// 再删屏清指针——由 pc_fui_switch_page 的退页钩子链落实。
//
// 内存所有权:字符串参数只在调用期间被读取;反馈页文案拷入本层
// 静态缓冲(构建同步完成,不跨调用保留调用方指针)。
#include "pc_ui.h"

#include <string.h>

#include "lvgl.h"

#include "pc_storage.h"
#include "pc_ui_fui.h"
#include "pc_ui_int.h"

/* ---- 当前页(与 pc_state_t 对应;反馈页为临时覆盖页) ---- */
typedef enum {
    PC_PAGE_STANDBY = 0,
    PC_PAGE_MENU,
    PC_PAGE_PRESENT,
    PC_PAGE_MEDIA,
    PC_PAGE_PAIR,
    PC_PAGE_SLEEP,
    PC_PAGE_FEEDBACK, /* 覆盖页:模式态不变,到时自动恢复 */
} pc_page_t;

static pc_page_t s_page = PC_PAGE_STANDBY;
static pc_state_t s_state = PC_ST_STANDBY_HOME;

/* ---- 读数缓存(页面重建时还原现场) ---- */
static char s_timer[8] = "00:00"; /* "MM:SS" */
static char s_host[17];           /* 已连接主机显示名 */
static bool s_link;               /* 连接状态 */
static int s_battery = -1;        /* -1 = 不可用(§8 降级) */
static int s_slide;               /* < 1 = 不画 */
static int s_volume = 50;         /* 音量估算(组装层接入前默认) */

/* 反馈页文案缓冲(构建期读取)。 */
static char s_fb_title[24];
static char s_fb_detail[48];

/* ---- 缓存访问器(实现页见各 pc_ui_fui_*.c,声明见 pc_ui_int.h) ---- */

bool pc_ui_cache_link(char *host_out, int cap)
{
    if (host_out != NULL && cap > 0) {
        lv_snprintf(host_out, (size_t)cap, "%s",
                    s_link ? s_host : "");
    }
    return s_link;
}

int pc_ui_cache_battery(void) { return s_battery; }
const char *pc_ui_cache_timer(void) { return s_timer; }
int pc_ui_cache_slide(void) { return s_slide; }
int pc_ui_cache_volume(void) { return s_volume; }

int pc_ui_cache_slot(void)
{
    /* 槽位归 NVS 配置;页面建屏时直读即可,这里提供统一出口。 */
    pc_cfg_t cfg;
    (void)pc_cfg_load(&cfg);
    return (int)cfg.slot;
}

/* ---- 构建器包装(转场用,无参签名) ---- */

static lv_obj_t *build_feedback_page(void)
{
    return pc_ui_media_feedback_build(s_fb_title, s_fb_detail);
}

/* ---- 接口 1:初始化 ---- */

esp_err_t pc_ui_init(void)
{
    pc_fui_init(); /* 背光档等转场前置 */

    s_state = PC_ST_STANDBY_HOME;
    s_page = PC_PAGE_STANDBY;
    s_battery = -1;
    s_slide = 0;
    s_link = false;
    s_host[0] = '\0';
    lv_snprintf(s_timer, sizeof(s_timer), "00:00");

    /* 首屏:一次性黑场转场建立待机主页(启动期屏幕内容未定,
     * 转场即首绘)。 */
    lv_obj_t *scr = pc_fui_switch_page(pc_ui_standby_build);
    if (scr == NULL) return ESP_ERR_NO_MEM; /* 24 KB 红线,规格 §9 */
    return ESP_OK;
}

/* ---- 接口 2:模式级页面切换 ---- */

/* 按状态取构建器;反馈覆盖页不在此列(由 show_feedback 驱动)。 */
static pc_fui_builder_t builder_of(pc_state_t st)
{
    switch (st) {
    case PC_ST_STANDBY_HOME: return pc_ui_standby_build;
    case PC_ST_STANDBY_MENU: return pc_ui_menu_build;
    case PC_ST_PRESENT:      return pc_ui_present_build;
    case PC_ST_MEDIA:        return pc_ui_media_build;
    case PC_ST_PAIR:         return pc_ui_pair_build;
    case PC_ST_SLEEP:        return pc_ui_sleep_build;
    default:                 return pc_ui_standby_build; /* 防御式 */
    }
}

static pc_page_t page_of(pc_state_t st)
{
    switch (st) {
    case PC_ST_STANDBY_HOME: return PC_PAGE_STANDBY;
    case PC_ST_STANDBY_MENU: return PC_PAGE_MENU;
    case PC_ST_PRESENT:      return PC_PAGE_PRESENT;
    case PC_ST_MEDIA:        return PC_PAGE_MEDIA;
    case PC_ST_PAIR:         return PC_PAGE_PAIR;
    case PC_ST_SLEEP:        return PC_PAGE_SLEEP;
    default:                 return PC_PAGE_STANDBY;
    }
}

void pc_ui_show_state(pc_state_t st)
{
    const bool was_feedback = (s_page == PC_PAGE_FEEDBACK);
    const pc_state_t prev = s_state;
    s_state = st;

    /* 同态且无反馈覆盖:屏幕已匹配,无需重建
     * (组装层只在状态变化时调用本接口,此分支为防御式)。 */
    if (!was_feedback && st == prev) return;

    /* 菜单进入规则:从非菜单态进入时选中项归零(与状态机
     * menu_sel 复位同步,见 pc_app_fsm.c);反馈返回菜单时
     * 维持原选中。 */
    if (st == PC_ST_STANDBY_MENU && prev != PC_ST_STANDBY_MENU) {
        pc_ui_menu_reset_sel();
    }

    s_page = page_of(st);
    (void)pc_fui_switch_page(builder_of(st));
}

/* ---- 接口 3:计时刷新(仅演示页,局部脏区 1-4 KB/s) ---- */

void pc_ui_set_timer(const char *text)
{
    if (text == NULL) return;
    lv_snprintf(s_timer, sizeof(s_timer), "%s", text);
    if (s_page == PC_PAGE_PRESENT) {
        pc_ui_present_set_timer(s_timer); /* 仅数字矩形重绘 */
    }
    /* 其它页调用:缓存即可,进入演示页时按缓存还原(降级)。 */
}

/* ---- 接口 4:页码刷新(仅演示页,事件驱动无周期重绘) ---- */

void pc_ui_set_slide(int page)
{
    s_slide = page;
    if (s_page == PC_PAGE_PRESENT) {
        pc_ui_present_set_slide(page);
    }
}

/* ---- 接口 5:链路刷新(事件驱动,无轮询重绘,§5) ---- */

void pc_ui_set_link(bool connected, const char *host)
{
    s_link = connected;
    lv_snprintf(s_host, sizeof(s_host), "%s",
                (connected && host != NULL) ? host : "");
    switch (s_page) {
    case PC_PAGE_STANDBY:
        pc_ui_standby_set_link(connected, s_host);
        break;
    case PC_PAGE_PRESENT:
        pc_ui_present_set_link(connected, s_host);
        break;
    default:
        break; /* 其它页:缓存即可,切页时还原 */
    }
}

/* ---- 接口 6:电量刷新(顶栏;-1 降级 §8) ---- */

void pc_ui_set_battery(int percent)
{
    s_battery = percent;
    /* 电池控件挂在顶栏,全部页面共用同一刷新入口。 */
    switch (s_page) {
    case PC_PAGE_STANDBY: pc_ui_standby_set_battery(percent); break;
    case PC_PAGE_MENU:    pc_ui_menu_set_battery(percent);    break;
    case PC_PAGE_PRESENT: pc_ui_present_set_battery(percent); break;
    case PC_PAGE_MEDIA:
    case PC_PAGE_FEEDBACK: pc_ui_media_set_battery(percent);  break;
    case PC_PAGE_PAIR:    pc_ui_pair_set_battery(percent);    break;
    case PC_PAGE_SLEEP:   pc_ui_sleep_set_battery(percent);   break;
    default: break;
    }
}

/* ---- 接口 7:6 位配对码(仅配对页,规格 §1/FR-07) ---- */

void pc_ui_show_passkey(uint32_t code)
{
    if (s_page == PC_PAGE_PAIR) {
        pc_ui_pair_show_passkey(code);
    }
    /* 非配对页:忽略(降级)。 */
}

/* ---- 配对页 "SLOT n/3" 局部刷新(C2 修复) ---- */

void pc_ui_set_pair_slot(int n)
{
    if (s_page == PC_PAGE_PAIR) {
        pc_ui_pair_set_slot(n);
    }
}

/* ---- 接口 8:1.5 s 动作反馈页(ui-design §4.4) ---- */

void pc_ui_show_feedback(const char *title, const char *detail)
{
    /* 文案拷入本层缓冲:构建在切页调用内同步完成。 */
    lv_snprintf(s_fb_title, sizeof(s_fb_title), "%s",
                title != NULL ? title : "");
    lv_snprintf(s_fb_detail, sizeof(s_fb_detail), "%s",
                detail != NULL ? detail : "");
    s_page = PC_PAGE_FEEDBACK; /* 模式态 s_state 不变 */
    (void)pc_fui_switch_page(build_feedback_page);
}

/* ---- M2 追加接口 1:菜单选中项(既有 8 接口签名未动) ---- */

void pc_ui_set_menu_sel(int sel)
{
    if (s_page == PC_PAGE_MENU) {
        pc_ui_menu_set_sel(sel); /* 仅新旧两行局部重绘 */
    }
}

/* ---- M2 追加接口 2:媒体音量读数 ---- */

void pc_ui_set_volume(int vol)
{
    /* D1 修复:钳制上限改为 100,与装配层 s_volume < 100U 的
     * 增量保护(见 pc_app_main.c FX_HID_CONSUMER 处理)对齐,
     * 避免装配层推到 100 后被 UI 错夹为 99。 */
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    if (s_page == PC_PAGE_MEDIA) {
        pc_ui_media_set_volume(vol);
    }
}
