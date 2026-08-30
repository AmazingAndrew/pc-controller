// main/pc_ui_fui.h
// PC Controller FUI(Cyberpunk HUD)主题核心与公共构件 —— 模块契约头。
//
// 事实源:
//   - docs/software-design/pc-controller/ui-design.md §2 设计令牌表、
//     §3 布局分区、§5 渲染纪律、§7 动效、§8 降级;
//   - 视觉基线为 fui-v6 系列样机(工作区临时文件,不入库)。
//
// 模块分工:
//   - 本模块 = 主题核心:令牌色值、整屏骨架(底纹/网格/外框/顶栏/
//     h1 标题)、面板/灯条/页脚/扫描线公共构件、一次性黑场转场;
//   - pc_ui_fui_standby/menu/present/pair/media.c = 五个页面,数据
//     驱动,不含业务逻辑;
//   - pc_ui.c = pc_ui.h 接口实现层,维护"当前页"并把刷新路由到页面。
//
// 渲染纪律硬约束(成文于此,全部页面与构件必须遵守,见 §5):
//   1. 进场一次性全屏绘制底纹(16 批 × 240×20),此后只刷脏区;
//   2. 禁止每帧全屏动画;禁止双缓冲(单 240×20 行缓冲红线);
//   3. 灯条/闪烁类动效半周期 >= 500 ms;
//   4. 扫描线仅 240×2 px 半透明亮带,脏区预算 < 1 KB/帧;
//   5. 页面转场 = 一次性黑场(降背光 -> 删旧屏 -> 建新屏 -> 恢复),
//      不做连续过渡动画;退页先停该屏全部动画/定时器再删屏
//      (规格 §7 行 164 退出顺序)。
//
// 字体占位纪律:
//   全部字号暂用 LVGL 内置 Montserrat / unscii-8 占位,统一声明:
//   "Placeholder font; final HUD pixel font pending OFL license
//   verification (requirements §15)."
//   不创建任何字体资产文件(.c 字模 / .ttf 均不提交)。
//
// 线程纪律:本模块全部接口假定调用方已持 bsp_lvgl_lock()
// (组装层统一加锁,见 pc_ui.h 文件头);内部不再二次加锁。
//
// 屏显文案纪律:优先走字符串表 (pc_str_en);页面专属图例文案使用
// 字面量时严格遵守表头字符集(大写 A-Z、数字、空格、':' '+' '-'
// '.' '/'),不引入小写与表外符号(未来字体子集的输入清单)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* ---- 设计令牌(ui-design §2,单缓冲 RGB565,lv_color_hex 转换) ----
 * 每一项注释逐一对照 §2 令牌表的十六进制值与用途。 */

/* BG       #0B1030 —— 全屏背景(深海军蓝) */
#define PC_FUI_C_BG        0x0B1030u
/* PANEL_BG #0D1338 —— 面板填充色 */
#define PC_FUI_C_PANEL_BG  0x0D1338u
/* PANEL_GLOW #F07818 —— 面板边框、发光、主强调色(橙) */
#define PC_FUI_C_GLOW      0xF07818u
/* STATUS   #FFD700 —— 状态字、按键名、h1、角标(荧光黄) */
#define PC_FUI_C_STATUS    0xFFD700u
/* LABEL    #3FE0F0 —— 面板标签、顶栏模式名、计时数字(青) */
#define PC_FUI_C_LABEL     0x3FE0F0u
/* TEXT     #EAF2FF —— 正文(白) */
#define PC_FUI_C_TEXT      0xEAF2FFu
/* FRAME    #2A2F55 —— 外框、页脚分隔线(蓝灰;也作副标题暗字色) */
#define PC_FUI_C_FRAME     0x2A2F55u
/* LAMP_OFF #1A2148 —— 灯条熄灭段底色 */
#define PC_FUI_C_LAMP_OFF  0x1A2148u
/* OK_GREEN #3FF08F —— 仅用于"稳定链路"指示,不得它用(§2/§4.2) */
#define PC_FUI_C_OK_GREEN  0x3FF08Fu
/* 告警红 —— §2 令牌集之外的状态局部色:仅用于断连 "LOST" 状态
 * (ui-design §4.2 state variants),任何常态界面不得使用。 */
#define PC_FUI_C_ALERT     0xFF3B4Eu

/* ---- 布局分区(ui-design §3:顶栏 0-32 / 内容 32-296 / 底栏
 * 296-320;四边 10 px 安全边距,文本与面板边缘不得越过) ---- */
#define PC_FUI_W           240   /* 屏宽 */
#define PC_FUI_H           320   /* 屏高 */
#define PC_FUI_SAFE        10    /* 安全边距 */
#define PC_FUI_TOPBAR_H    32    /* 顶栏高度 */
#define PC_FUI_FOOTER_Y    296   /* 底栏起始 y */

/* ---- 灯条 ---- */
#define PC_FUI_LAMP_N      6     /* 段数 */
#define PC_FUI_LAMP_W      12    /* 段宽 12 px(§2) */
#define PC_FUI_LAMP_H      5     /* 段高 5 px(§2) */
#define PC_FUI_LAMP_GAP    2     /* 段间距 */

/* 灯条点亮语义(§2:橙发光 / 青发光两种;熄灭用 LAMP_OFF) */
typedef enum {
    PC_FUI_LAMP_OFF = 0,  /* 熄灭:全段 LAMP_OFF */
    PC_FUI_LAMP_ORANGE,   /* 橙发光:PANEL_GLOW */
    PC_FUI_LAMP_CYAN,     /* 青发光:LABEL */
} pc_fui_lamp_mode_t;

/* 页脚按键图例条目(§2 底栏:键名荧光黄 + 动作文白) */
typedef struct {
    const char *key;    /* 键名,如 "OK" / "UP" / "DOWN" */
    const char *action; /* 动作,如 "FULLSCR" / "HOLD EXIT" */
} pc_fui_footer_entry_t;

/* 页脚最多 3 组图例(三键设备图例上限) */
#define PC_FUI_FOOTER_MAX 3

/* ---- 整屏骨架 ---- */

/* 创建整屏骨架:BG 底色 + 网格底纹(一次性绘制,§5 规则 1)+
 * 1 px FRAME 外框 + 顶栏(左侧青色发光模式标签 + 右侧电池控件)。
 * 参数:mode 顶栏模式名文案(如 "PC-CTRL" / "PRESENT",仅调用期间读取)。
 * 返回值:新建屏幕对象;LVGL 内存不足返回 NULL(调用方按 24 KB
 *        红线降级,规格 §9)。 */
lv_obj_t *pc_fui_screen_create(const char *mode);

/* 取当前活动屏幕(转场期间可能为 NULL)。 */
lv_obj_t *pc_fui_screen(void);

/* 刷新顶栏电量(所有页面共用)。
 * 参数:percent 0..100;收到 -1 时按 §8 降级:只画电池外框,
 *       绝不画数字(不画 "-1" 也不画 "0%")。 */
void pc_fui_set_battery(int percent);

/* ---- 公共构件 ---- */

/* 面板(§2 构件规则):2 px PANEL_GLOW 边框 + 内外发光(以
 * lv_style shadow 模拟:外发光 = 大宽度低透明度橙影,内发光 =
 * 小宽度较高透明度橙影)+ 对角双角标(左上/右下,2 px 荧光黄
 * 括号)+ 左上青色面板标签。面板内容区自标签行之下开始。
 * 参数:parent 屏幕;w/h 面板尺寸;label 面板标签文案(如
 *       "HOST LINK")。x/y 用 lv_obj_set_pos 由调用方随后定位。
 * 返回值:面板对象;失败返回 NULL。 */
lv_obj_t *pc_fui_panel_create(lv_obj_t *parent, int w, int h,
                              const char *label);

/* 灯条:6 段 12×5 px,横向居中放在 (x, y)。
 * 返回值:灯条容器(宽 = 6×12 + 5×2 = 82,高 5);失败返回 NULL。
 * 语义用 pc_fui_lamp_paint() 整组着色;逐段动画(渐进填充/橙青
 * 交替)由页面侧持有段指针数组自行驱动。 */
lv_obj_t *pc_fui_lamp_create(lv_obj_t *parent, int x, int y);

/* 灯条整组着色:off / 橙 / 青 三态一次刷全段。 */
void pc_fui_lamp_paint(lv_obj_t *lamp, pc_fui_lamp_mode_t mode);

/* 底栏按键图例:1 px FRAME 分隔线(§3)+ 图例行。键名荧光黄、
 * 动作文白、8 px 占位字体、等距排布。
 * 参数:scr 屏幕;entries 图例数组;count 条数(<= 3);
 *       out_actions 可为非 NULL:回填各条"动作"标签指针(供
 *       连接态切换等局部改词使用),容量须 >= count。
 * 返回值:图例容器对象;失败返回 NULL。 */
lv_obj_t *pc_fui_footer_create(lv_obj_t *scr,
                               const pc_fui_footer_entry_t *entries,
                               int count, lv_obj_t **out_actions);

/* 扫描线亮带(§7):240×2 px 半透明白带沿 y 轴缓慢下移,
 * lv_anim 循环,周期 3 s(规格 2-4 s 区间内)。
 * 渲染纪律注释:亮带脏区 = 240×2×2 B = 960 B < 1 KB/帧(§5
 * 预算表);亮带必须最后创建,保证覆盖在全部构件之上。 */
void pc_fui_scanline_create(lv_obj_t *scr);

/* 标签助手:字体 + 颜色(+ 可选对齐)。字号均为占位字体,见文件头
 * "Placeholder font; final HUD pixel font pending OFL license
 * verification (requirements §15)." */
lv_obj_t *pc_fui_label(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, lv_color_t color);

/* ---- 页面生命周期 ---- */

/* 页面构建器:建屏并返回(不载入);失败返回 NULL。
 * 页面只读取 pc_ui 层的缓存读数建屏,不直接访问组装层状态。 */
typedef lv_obj_t *(*pc_fui_builder_t)(void);

/* 一次性黑场转场切页(§5 规则 4):
 *   1. 退页先停旧屏全部动画与定时器(规格 §7 行 164 退出顺序);
 *   2. 降背光到最低可见档;
 *   3. 删旧屏并清空内部指针;
 *   4. 调 build 建新屏并载入;
 *   5. 恢复背光到配置档。
 * 不做任何连续过渡动画。构建失败时保留可显示的最小降级屏。
 * 返回值:新屏对象;失败返回 NULL。 */
lv_obj_t *pc_fui_switch_page(pc_fui_builder_t build);

/* 退页钩子注册:页面建屏时登记自己的离场清理函数(停自建的灯条
 * 定时器/动画并清空静态指针),切页时在建新屏之前调用一次。
 * 传 NULL 清除登记。 */
void pc_fui_set_leave_cb(void (*cb)(void));

/* 背光档初始化:从 NVS 配置读用户背光档(失败用默认值),供
 * 转场"恢复背光"使用。启动阶段持锁调用一次。 */
void pc_fui_init(void);
