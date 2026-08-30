// tests/test_pc_host_profiles.c
// 主机配置表 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 §1/FR-03 三系统锁屏组合键与 §11 数据驱动扩展:
//   Windows = Win + L、macOS = Ctrl + Cmd + Q、Linux = Super + L;
// 越界降级语义见头文件:未知配置返回 NULL,宁可不锁屏也不发错组合。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_host_profiles.c main/pc_host_profiles.c
#include <assert.h>
#include <string.h>

#include "pc_host_profiles.h"

int main(void)
{
    const pc_combo_t *c;

    /* ======== Windows:Win + L ======== */
    c = pc_lock_combo(PC_OS_WINDOWS);
    assert(c != NULL);
    assert(c->mods == PC_MOD_LGUI);       /* 0x08 */
    assert(c->keycode == PC_KEY_L);       /* 0x0F */
    assert(strcmp(pc_profile_name(PC_OS_WINDOWS), "WINDOWS") == 0);
    assert(strcmp(pc_combo_text(PC_OS_WINDOWS), "WIN+L") == 0);

    /* ======== macOS:Ctrl + Cmd + Q ======== */
    c = pc_lock_combo(PC_OS_MACOS);
    assert(c != NULL);
    assert(c->mods == (PC_MOD_LCTRL | PC_MOD_LGUI)); /* 0x09 */
    assert(c->keycode == PC_KEY_Q);                  /* 0x14 */
    assert(strcmp(pc_profile_name(PC_OS_MACOS), "MACOS") == 0);
    assert(strcmp(pc_combo_text(PC_OS_MACOS), "CTRL+CMD+Q") == 0);

    /* ======== Linux:Super + L(主流桌面环境通用) ======== */
    c = pc_lock_combo(PC_OS_LINUX);
    assert(c != NULL);
    assert(c->mods == PC_MOD_LGUI); /* 0x08,与 Windows 同构 */
    assert(c->keycode == PC_KEY_L); /* 0x0F */
    assert(strcmp(pc_profile_name(PC_OS_LINUX), "LINUX") == 0);
    assert(strcmp(pc_combo_text(PC_OS_LINUX), "SUPER+L") == 0);

    /* ======== 越界 / 未知配置:三接口一律 NULL(规格 §1/FR-03:
     * 不得向不同配置的主机发错组合键) ======== */
    assert(pc_lock_combo(PC_OS_MAX) == NULL);
    assert(pc_profile_name(PC_OS_MAX) == NULL);
    assert(pc_combo_text(PC_OS_MAX) == NULL);

    assert(pc_lock_combo((pc_os_t)(PC_OS_MAX + 1)) == NULL);
    assert(pc_profile_name((pc_os_t)99) == NULL);
    assert(pc_combo_text((pc_os_t)-1) == NULL); /* 未初始化/脏配置的负值 */

    return 0;
}
