// main/pc_strings.h
// 屏显字符串表(平台无关纯逻辑模块)。
//
// 设计:索引式字符串表——UI 代码只持有枚举索引 (pc_str_t),
// 文案集中在按语言组织的表中。当前基线为纯英文
// (规格 §1 非目标:English only,本版本不引入 CJK 字体);
// 中文本地化的接入点已在下方预留(规格 §11 扩展性第 4 条:
// "按索引预留第二张表,后续中文化无需重构 UI 代码")。
//
// 字符集约束(即未来字体子集的输入清单):
// 本表全部文案只使用以下 ASCII 字符,生成像素字体子集时
// 以此为准(注意:不含小写字母,不含 CJK):
//   'A'-'Z'、'0'-'9'、空格、':'、'+'、'-'、'.'、'/'
//
// 平台无关性:仅依赖 C11 标准库,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

/* 屏显文案索引。枚举顺序即表索引,新增条目必须追加在
 * PC_STR_COUNT 之前,且与 .c 中表定义逐项同步。 */
typedef enum {
    /* ---- 模式名(各模式页头) ---- */
    PC_STR_MODE_STANDBY = 0, /* 待机主页模式名 */
    PC_STR_MODE_PRESENT,     /* 演示模式名 */
    PC_STR_MODE_MEDIA,       /* 媒体模式名 */
    PC_STR_MODE_PAIR,        /* 配对模式名 */
    PC_STR_MODE_SLEEP,       /* 睡眠模式名(唤醒瞬间的过渡页) */

    /* ---- 面板标签(演示页信息区) ---- */
    PC_STR_LABEL_TIMER,   /* 演讲计时器标签 */
    PC_STR_LABEL_PAGE,    /* 页码标签 */
    PC_STR_LABEL_SLOT,    /* 槽位标签 */
    PC_STR_LABEL_BATTERY, /* 电量标签 */

    /* ---- 状态词 ---- */
    PC_STR_STATUS_CONNECTED,    /* BLE 已连接 */
    PC_STR_STATUS_DISCONNECTED, /* BLE 已断开 */
    PC_STR_STATUS_NO_BOND,      /* 无任何历史绑定(引导配对) */

    /* ---- 待机菜单 8 项。顺序与索引必须与规格 §6 行 135 的
     * 菜单顺序一致:0 PAIRING、1 CLEAR SLOT、2 SLOT、
     * 3 HOST PROFILE、4 KEY SOUND、5 BACKLIGHT、
     * 6 MEDIA MODE、7 ABOUT。 ---- */
    PC_STR_MENU_PAIRING,      /* 菜单项 0:配对 */
    PC_STR_MENU_CLEAR_SLOT,   /* 菜单项 1:清除槽位绑定 */
    PC_STR_MENU_SLOT,         /* 菜单项 2:切换槽位 */
    PC_STR_MENU_HOST_PROFILE, /* 菜单项 3:主机配置 */
    PC_STR_MENU_KEY_SOUND,    /* 菜单项 4:按键音开关 */
    PC_STR_MENU_BACKLIGHT,    /* 菜单项 5:背光设置 */
    PC_STR_MENU_MEDIA_MODE,   /* 菜单项 6:进入媒体模式 */
    PC_STR_MENU_ABOUT,        /* 菜单项 7:关于页 */

    /* ---- 配置取值词 ---- */
    PC_STR_VALUE_ON,  /* 开 */
    PC_STR_VALUE_OFF, /* 关 */

    /* ---- 待机主页图例(按键提示) ---- */
    PC_STR_HINT_MENU,    /* 打开菜单 */
    PC_STR_HINT_PRESENT, /* 进入演示(已连接) */
    PC_STR_HINT_PAIR,    /* 进入配对(未连接) */
    PC_STR_HINT_LOCK,    /* 长按锁屏 */
    PC_STR_HINT_BACK,    /* 长按返回待机(演示/媒体/菜单页) */
    PC_STR_HINT_CANCEL,  /* 取消(配对页) */

    /* ---- 反馈词(短暂屏显/结果提示) ---- */
    PC_STR_FB_LOCKED,       /* 锁屏已触发 */
    PC_STR_FB_PAIR_OK,      /* 配对成功 */
    PC_STR_FB_PAIR_FAIL,    /* 配对失败 */
    PC_STR_FB_SLOT_CLEARED, /* 槽位绑定已清除 */
    PC_STR_FB_SAVED,        /* 配置已保存 */
    PC_STR_FB_PASSKEY,      /* 配对码标签(其后跟 6 位数字) */

    /* ---- 关于页 ---- */
    PC_STR_ABOUT_APP,     /* 应用名 */
    PC_STR_ABOUT_VERSION, /* 版本号 */

    /* 数量哨兵:不表示真实文案,用作表长度与越界检查。 */
    PC_STR_COUNT
} pc_str_t;

/* 英文字符串表。索引与 pc_str_t 一一对应,全部为大写
 * 纯 ASCII(字符集见文件头)。表项为静态只读字面量,
 * 生命周期为整个运行期。 */
extern const char *const pc_str_en[PC_STR_COUNT];

/* ---- 中文表预留位(规格 §11) ----
 * 未来中文化时在此声明同索引的第二张表:
 *   extern const char *const pc_str_zh[PC_STR_COUNT];
 * 并要求其字体为覆盖全部实际字符的 CJK 子集
 * (编码约定:中文方框字陷阱——必须先编入含全部实际字符的
 * CJK 子集字体,见 docs/development/coding-conventions.zh_CN.md)。
 * 当前基线不定义该符号,避免未使用符号占用 Flash。 */
