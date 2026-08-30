// main/pc_host_profiles.c
// 主机配置表实现:三张并列的静态只读表,按 pc_os_t 索引。
// 新增/修改条目时三张表必须同步,索引一一对应。
//
// 组合键数值依据(规格 §1/FR-03):
//   Windows 锁屏 = Win + L        -> 修饰位 LGUI(0x08)        + 键码 L(0x0F)
//   macOS   锁屏 = Ctrl + Cmd + Q -> 修饰位 LCTRL|LGUI(0x09)  + 键码 Q(0x14)
//   Linux   锁屏 = Super + L      -> 修饰位 LGUI(0x08)        + 键码 L(0x0F)
// 修饰位/键码常量出处见 pc_hid_reports.h(HID Usage Tables)。
//
// 平台无关:仅依赖 C11 标准库与本目录纯逻辑头,可被 host 测试
// 直接编译。
#include "pc_host_profiles.h"

#include <stddef.h>

/* 锁屏组合键表。索引与 pc_os_t 枚举值一一对应。 */
static const pc_combo_t s_lock_combos[PC_OS_MAX] = {
    /* PC_OS_WINDOWS */ { PC_MOD_LGUI, PC_KEY_L },
    /* PC_OS_MACOS   */ { PC_MOD_LCTRL | PC_MOD_LGUI, PC_KEY_Q },
    /* PC_OS_LINUX   */ { PC_MOD_LGUI, PC_KEY_L },
};

/* 配置档案名表(屏显/日志用,大写纯 ASCII)。 */
static const char *const s_profile_names[PC_OS_MAX] = {
    /* PC_OS_WINDOWS */ "WINDOWS",
    /* PC_OS_MACOS   */ "MACOS",
    /* PC_OS_LINUX   */ "LINUX",
};

/* 锁屏组合键人类可读文案表(屏显提示用)。 */
static const char *const s_combo_texts[PC_OS_MAX] = {
    /* PC_OS_WINDOWS */ "WIN+L",
    /* PC_OS_MACOS   */ "CTRL+CMD+Q",
    /* PC_OS_LINUX   */ "SUPER+L",
};

/* 索引守卫:防御式处理负值/越界枚举(调用方可能透传未初始化
 * 的 NVS 配置)。返回值合法时表项必然存在。 */
static bool os_in_range(pc_os_t os)
{
    return (int)os >= 0 && (int)os < (int)PC_OS_MAX;
}

const pc_combo_t *pc_lock_combo(pc_os_t os)
{
    return os_in_range(os) ? &s_lock_combos[(int)os] : NULL;
}

const char *pc_profile_name(pc_os_t os)
{
    return os_in_range(os) ? s_profile_names[(int)os] : NULL;
}

const char *pc_combo_text(pc_os_t os)
{
    return os_in_range(os) ? s_combo_texts[(int)os] : NULL;
}
