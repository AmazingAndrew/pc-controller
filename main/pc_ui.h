// main/pc_ui.h
// PC Controller UI 接线口(最小接口占位)。
//
// 现状说明:正式的 FUI 赛博朋克五页(待机/演示/配对/媒体/反馈,
// 见 docs/software-design/pc-controller/ui-design.md)已在 M2 落地:
// 实现层 pc_ui.c + 主题核心 pc_ui_fui.c/.h + 五页
// pc_ui_fui_standby/menu/present/pair/media.c(临时桩 pc_ui_stub.c
// 已整文件替换移除)。
//
// 线程纪律(规格 §7 行 159、ui-design §8):
//   全部接口必须在持有 bsp_lvgl_lock() 的上下文调用(组装层
//   在非 LVGL 任务中已统一加锁);接口内部不再二次加锁。
//   显示初始化失败时组装层不会创建/调用本模块(无屏降级,
//   规格 §10),接口无需防御"无显示"场景。
//
// 页面退出顺序(规格 §7 行 164):先停可能访问 UI 的任务/定时器,
// 再删屏清指针——由 pc_ui_show_state() 的页面切换实现内部遵守。
//
// 内存所有权:字符串参数只在调用期间被读取,本模块不保留指针。
//
// M2 接口扩展记录:本头文件原定义 8 个接口;M2(FUI 五页)落地时
// 在文件末尾【追加】了 2 个接口(菜单选中项刷新、媒体音量刷新),
// 既有 8 个接口的签名逐字未动(组装层零改动)。追加原因:
//   - 菜单选中项索引与音量计数都由组装层状态机持有,仅凭既有
//     8 接口无法把读数推给屏幕,且"每次按键重建整页"违反渲染纪律。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pc_key_semantics.h" /* pc_state_t */

/* 初始化:创建首屏(按当前状态)并载入。
 * 前置:bsp_display_init() + bsp_lvgl_init() 已成功;调用方持锁。
 * 返回值:
 *   ESP_OK                屏幕已建立;
 *   ESP_ERR_NO_MEM        LVGL 内存池耗尽(24 KB 红线,规格 §9)。
 * 线程上下文:启动阶段,调用方持锁。 */
esp_err_t pc_ui_init(void);

/* 切换到指定模式的页面(待机主页/菜单/演示/媒体/配对/反馈的
 * 模式级切换)。内部按"先删旧屏、再建新屏"的一次性黑屏过渡
 * 执行(ui-design §5 规则 4)。
 * 参数:st 当前应用状态;SLEEP 态显示最简过渡页。
 * 线程上下文:应用任务,调用方持锁;无阻塞、无重活。 */
void pc_ui_show_state(pc_state_t st);

/* 刷新演讲计时器读数。
 * 参数:text "MM:SS" / "HH:MM:SS" 自适应格式 (pc_speech_format 的
 *       输出, <= 9 字符)。仅演示页有该区域; 其它页调用被忽略
 *       (降级)。 */
void pc_ui_set_timer(const char *text);

/* 刷新页码读数(本地估算,规格 §1/FR-12)。
 * 参数:page >= 1 显示页码;< 1(未进全屏/来源不可用)不画页码。 */
void pc_ui_set_slide(int page);

/* 刷新链路状态。
 * 参数:
 *   connected 是否存在活动连接;
 *   host      已连接主机显示名(可空串;断开时忽略)。 */
void pc_ui_set_link(bool connected, const char *host);

/* 刷新电量(顶栏)。
 * 参数:percent 0..100;pc_ui_set_battery 收到 -1 时按降级语义
 *       只画图标轮廓不画数字(规格 §10)。 */
void pc_ui_set_battery(int percent);

/* 显示 6 位配对码(规格 §1/FR-07)。
 * 参数:code 100000..999999。 */
void pc_ui_show_passkey(uint32_t code);

/* 配对页副标题 "SLOT n/3" 刷新(C2 修复):仅在配对页在屏时
 * 生效;其它页调用被忽略(降级)。装配层在处理 PC_FX_SLOT_SWITCH
 * 且当前状态为 PC_ST_PAIR 时调用,用于切槽后只重绘一行(遵堡
 * ui-design §5 脏区纪律,不重建整页)。
 * 参数:n 1..PC_SLOT_COUNT,越界钳制。 */
void pc_ui_set_pair_slot(int n);

/* 显示 1.5 s 动作反馈页(锁屏/槽位清除/保存等,
 * ui-design §4.4/§7:到时自动返回,定时器由组装层管理)。
 * 参数:
 *   title  反馈主词(如 "LOCKED");
 *   detail 副行文案(如 "WIN+L / PROFILE: WINDOWS"),可为空串。
 * 内存所有权:两参数仅调用期间被读取。 */
void pc_ui_show_feedback(const char *title, const char *detail);

/* ---- M2 追加接口(既有 8 接口签名未动) ---- */

/* 刷新菜单选中项高亮(仅菜单页在屏时生效;其它页忽略)。
 * 参数:sel 选中项索引 0..9(10 项菜单:8 项规格 + #42 RESET BLE +
 *       #46 SCREENSHOT);
 *       越界值按 0 处理(防御式)。仅局部重绘新旧两行高亮,
 *       不重建整页(渲染纪律:脏区刷新,ui-design §5)。 */
void pc_ui_set_menu_sel(int sel);

/* 刷新媒体模式音量读数(仅媒体页在屏时生效;其它页忽略,
 * 读数由本层缓存,下次进媒体页时带上)。
 * 参数:vol 音量 0..100;越界钳制。
 * 接线状态(D3 修复):装配层已在 pc_app_main.c 的 PC_FX_HID_
 * CONSUMER 与 PC_FX_ENTER_MEDIA 处理点接入本接口,负责本地音
 * 量计数器(Consumer 协议无绝对音量回读,见 FR-04)与屏显同步。 */
void pc_ui_set_volume(int vol);
