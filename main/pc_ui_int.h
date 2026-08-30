// main/pc_ui_int.h
// PC Controller UI 内部协作头(仅 main/ 内使用,不属于对外契约)。
//
// 内容:
//   1. pc_ui.c 维护的"最近读数缓存"访问器——页面建屏时按缓存
//      还原现场,不直接访问组装层状态(页面数据驱动原则);
//   2. 六个页面(待机/菜单/演示/配对/媒体反馈/睡眠过渡)的
//      构建器与局部刷新函数原型。
//
// 线程契约:与 pc_ui.h 一致,全部调用方已持 bsp_lvgl_lock();
// 页面静态对象指针在退页钩子中清空(编码约束)。
//
// 渲染纪律提示(各页实现处逐条成文):进场一次性全屏绘制底纹,
// 此后仅脏区刷新;禁止每帧全屏动画;禁止双缓冲;灯条半周期
// >= 500 ms;扫描线仅 240×2 px 亮带。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* ---- 缓存访问器(实现于 pc_ui.c) ---- */

/* 链路缓存:返回连接状态;connected 时把主机显示名拷入 host_out
 * (容量 cap,含结尾符),未连接置空串。 */
bool pc_ui_cache_link(char *host_out, int cap);

/* 电量缓存:0..100;-1 = 不可用(§8 降级语义)。 */
int pc_ui_cache_battery(void);

/* 计时缓存:"MM:SS" 静态缓冲指针(生命周期 = 运行期)。 */
const char *pc_ui_cache_timer(void);

/* 页码缓存:>= 1 显示;< 1 不画(规格 §1/FR-12)。 */
int pc_ui_cache_slide(void);

/* 音量缓存:0..99(组装层接入前为默认值 50)。 */
int pc_ui_cache_volume(void);

/* 当前槽位缓存:0..2(组装层切槽时更新)。 */
int pc_ui_cache_slot(void);

/* ---- 待机主页(§4.1;实现见 pc_ui_fui_standby.c) ---- */
lv_obj_t *pc_ui_standby_build(void);
void pc_ui_standby_set_link(bool connected, const char *host);
void pc_ui_standby_set_battery(int percent);

/* ---- 菜单页(§4.1 menu;实现见 pc_ui_fui_menu.c) ---- */
lv_obj_t *pc_ui_menu_build(void);
void pc_ui_menu_set_sel(int sel);
void pc_ui_menu_reset_sel(void); /* 从非菜单态进入时选中项归零 */
void pc_ui_menu_set_battery(int percent);

/* ---- 演示页(§4.2;实现见 pc_ui_fui_present.c) ---- */
lv_obj_t *pc_ui_present_build(void);
void pc_ui_present_set_timer(const char *text);
void pc_ui_present_set_slide(int page);
void pc_ui_present_set_link(bool connected, const char *host);
void pc_ui_present_set_battery(int percent);

/* ---- 配对页(§4.3;实现见 pc_ui_fui_pair.c) ---- */
lv_obj_t *pc_ui_pair_build(void);
void pc_ui_pair_set_battery(int percent);
void pc_ui_pair_show_passkey(uint32_t code);
void pc_ui_pair_set_slot(int n); /* C2 修复:切槽后仅重绘副标题"SLOT n/3" */

/* ---- 媒体/动作反馈共用页(§4.4;实现见 pc_ui_fui_media.c) ---- */
lv_obj_t *pc_ui_media_build(void);
lv_obj_t *pc_ui_media_feedback_build(const char *title, const char *detail);
void pc_ui_media_set_battery(int percent);
void pc_ui_media_set_volume(int vol);

/* ---- 睡眠过渡页(最简页;实现见 pc_ui_fui_media.c 末尾) ---- */
lv_obj_t *pc_ui_sleep_build(void);
void pc_ui_sleep_set_battery(int percent);
