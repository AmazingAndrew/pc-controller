// main/pc_strings.c
// 屏显字符串表定义。表项顺序必须与头文件 pc_str_t 枚举逐项一致;
// 增删文案时两处同步修改。
//
// 全部文案仅使用文件头声明的字符集(大写 A-Z、数字、空格、
// ':'、'+'、'-'、'.'、'/'),不得引入小写或其它符号,
// 否则需同步更新字体子集清单。
//
// 平台无关:仅依赖本模块头文件,可被 host 测试直接编译。
#include "pc_strings.h"

/* 英文字符串表。注释标明每项的使用位置,便于 UI 与文案校对。 */
const char *const pc_str_en[PC_STR_COUNT] = {
    /* ---- 模式名 ---- */
    "STANDBY",         /* PC_STR_MODE_STANDBY:待机主页页头 */
    "PRESENT",         /* PC_STR_MODE_PRESENT:演示模式页头 */
    "MEDIA",           /* PC_STR_MODE_MEDIA:媒体模式页头 */
    "PAIR",            /* PC_STR_MODE_PAIR:配对模式页头 */
    "SLEEP",           /* PC_STR_MODE_SLEEP:睡眠过渡页 */

    /* ---- 面板标签 ---- */
    "TIMER",           /* PC_STR_LABEL_TIMER:演讲计时器前缀 */
    "PAGE",            /* PC_STR_LABEL_PAGE:页码前缀(后跟估算页码) */
    "SLOT",            /* PC_STR_LABEL_SLOT:槽位前缀(后跟 1/2/3) */
    "BATTERY",         /* PC_STR_LABEL_BATTERY:电量前缀(后跟百分比) */

    /* ---- 状态词 ---- */
    "CONNECTED",       /* PC_STR_STATUS_CONNECTED:连接状态指示 */
    "DISCONNECTED",    /* PC_STR_STATUS_DISCONNECTED:断连状态指示 */
    "NO BOND",         /* PC_STR_STATUS_NO_BOND:无绑定,引导配对 */

    /* ---- 待机菜单 8 项(顺序即菜单显示顺序,规格 §6 行 135) ---- */
    "PAIRING",         /* PC_STR_MENU_PAIRING:菜单项 0 */
    "CLEAR SLOT",      /* PC_STR_MENU_CLEAR_SLOT:菜单项 1 */
    "SLOT",            /* PC_STR_MENU_SLOT:菜单项 2 */
    "HOST PROFILE",    /* PC_STR_MENU_HOST_PROFILE:菜单项 3 */
    "KEY SOUND",       /* PC_STR_MENU_KEY_SOUND:菜单项 4 */
    "BACKLIGHT",       /* PC_STR_MENU_BACKLIGHT:菜单项 5 */
    "MEDIA MODE",      /* PC_STR_MENU_MEDIA_MODE:菜单项 6 */
    "ABOUT",           /* PC_STR_MENU_ABOUT:菜单项 7 */
    "RESET BLE",       /* PC_STR_MENU_RESET_BLE:菜单项 8,#42 两步式重置 */
    "SCREENSHOT",      /* PC_STR_MENU_SCREENSHOT:菜单项 9,#46 串口截屏 */

    /* ---- 配置取值词 ---- */
    "ON",              /* PC_STR_VALUE_ON:开关类配置"开" */
    "OFF",             /* PC_STR_VALUE_OFF:开关类配置"关" */

    /* ---- 待机主页图例 ---- */
    "UP: MENU",        /* PC_STR_HINT_MENU:UP 键打开菜单 */
    "OK: PRESENT",     /* PC_STR_HINT_PRESENT:OK 键进入演示(已连接) */
    "OK: PAIR",        /* PC_STR_HINT_PAIR:OK 键进入配对(未连接) */
    "HOLD OK: LOCK",   /* PC_STR_HINT_LOCK:长按 OK 锁屏 */
    "HOLD OK: BACK",   /* PC_STR_HINT_BACK:长按 OK 返回待机 */
    "OK: CANCEL",      /* PC_STR_HINT_CANCEL:配对页取消 */

    /* ---- 反馈词 ---- */
    "LOCKED",          /* PC_STR_FB_LOCKED:锁屏已发送 */
    "PAIRED",          /* PC_STR_FB_PAIR_OK:配对成功 */
    "PAIR FAILED",     /* PC_STR_FB_PAIR_FAIL:配对失败 */
    "SLOT CLEARED",    /* PC_STR_FB_SLOT_CLEARED:槽位绑定已清除 */
    "SAVED",           /* PC_STR_FB_SAVED:配置已写入 */
    "PASSKEY",         /* PC_STR_FB_PASSKEY:配对码标签(后跟 6 位数字) */
    "ARMED",           /* PC_STR_FB_RESET_ARM:#42 BLE 重置已武装 */
    "RESETTING",       /* PC_STR_FB_RESET_CONFIRM:#42 二次确认后短暂反馈 */

    /* ---- 关于页 ---- */
    "PC CONTROLLER",   /* PC_STR_ABOUT_APP:应用名 */
    "V1.0",            /* PC_STR_ABOUT_VERSION:版本号(发版时同步更新) */
};

/* 中文表预留位(规格 §11):当前基线 English only,不定义
 * pc_str_zh。中文化时按同索引新增第二张表并更新字体子集
 * (见头文件注释与编码约定"中文方框字陷阱")。 */
