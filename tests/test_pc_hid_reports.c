// tests/test_pc_hid_reports.c
// HID 报告层 host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:
//   - 报告布局 [修饰位, 保留, 6 键码]:pc_hid_reports.h 报告结构注释,
//     与 HID 1.11 标准键盘报告一致;
//   - 三系统锁屏组合键:规格 §1/FR-03(Win+L / Ctrl+Cmd+Q / Super+L),
//     数值经 pc_host_profiles 的组合键表给出;
//   - Report Map 双报告 Report ID:HOGP 多报告路由
//     (键盘 = 1,Consumer = 2,见 pc_hid_reports.h PC_REPORT_ID_*)。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_hid_reports.c main/pc_hid_reports.c main/pc_host_profiles.c
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pc_hid_reports.h"
#include "pc_host_profiles.h"

int main(void)
{
    pc_kbd_report_t r;
    uint8_t bytes[sizeof(pc_kbd_report_t)];
    uint8_t consumer[2];
    uint8_t map[128];
    int map_len;
    int i;

    /* ======== 报告布局:字节序 [mods, 0, k0..k5] ======== */

    /* 结构体内存布局即报告字节序:
     * 偏移 0 = 修饰位,偏移 1 = 保留字节,偏移 2..7 = 6 个键码。 */
    assert(offsetof(pc_kbd_report_t, mods) == 0);
    assert(offsetof(pc_kbd_report_t, reserved) == 1);
    assert(offsetof(pc_kbd_report_t, keys) == 2);
    assert(sizeof(pc_kbd_report_t) == 8);

    /* ======== pc_kbd_clear:全零报告 ======== */
    memset(&r, 0xA5, sizeof(r));
    pc_kbd_clear(&r);
    assert(r.mods == 0);
    assert(r.reserved == 0);
    for (i = 0; i < 6; i++) {
        assert(r.keys[i] == 0);
    }

    /* ======== pc_kbd_add:追加 / 修饰位合并 / 保留字节恒 0 ======== */
    pc_kbd_clear(&r);
    assert(pc_kbd_add(&r, PC_MOD_LGUI, PC_KEY_L));
    assert(r.mods == PC_MOD_LGUI);
    assert(r.keys[0] == PC_KEY_L);
    assert(r.reserved == 0); /* 保留字节不得被写入 */

    /* 追加第二个键:修饰位按位或合并,键码进下一个空槽。 */
    assert(pc_kbd_add(&r, PC_MOD_LCTRL, PC_KEY_Q));
    assert(r.mods == (PC_MOD_LGUI | PC_MOD_LCTRL));
    assert(r.keys[1] == PC_KEY_Q);

    /* 重复键忽略:返回 false 且报告不被修改。 */
    memcpy(bytes, &r, sizeof(bytes));
    assert(!pc_kbd_add(&r, 0, PC_KEY_L));
    assert(memcmp(&r, bytes, sizeof(bytes)) == 0);

    /* 键码 0 非法(0 = 空槽占位):拒绝且报告不变。 */
    assert(!pc_kbd_add(&r, 0, 0x00));
    assert(memcmp(&r, bytes, sizeof(bytes)) == 0);

    /* 6 键填满:再补 4 个不同键码到满。 */
    assert(pc_kbd_add(&r, 0, 0x04)); /* a */
    assert(pc_kbd_add(&r, 0, 0x05)); /* b */
    assert(pc_kbd_add(&r, 0, 0x06)); /* c */
    assert(pc_kbd_add(&r, 0, 0x07)); /* d */
    assert(r.keys[2] == 0x04 && r.keys[3] == 0x05);
    assert(r.keys[4] == 0x06 && r.keys[5] == 0x07);

    /* 第 7 键超出 6KRO:返回 false(ErrorRollOver 语义),报告不变。 */
    memcpy(bytes, &r, sizeof(bytes));
    assert(!pc_kbd_add(&r, PC_MOD_LSHIFT, 0x08));
    assert(memcmp(&r, bytes, sizeof(bytes)) == 0);

    /* ======== 三系统锁屏字节(规格 §1/FR-03,经 pc_host_profiles) ======== */

    /* Windows:Win + L = 修饰位 0x08 + 键码 0x0F。 */
    {
        const pc_combo_t *c = pc_lock_combo(PC_OS_WINDOWS);
        assert(c != NULL);
        pc_kbd_clear(&r);
        assert(pc_kbd_add(&r, c->mods, c->keycode));
        memcpy(bytes, &r, sizeof(bytes));
        assert(bytes[0] == 0x08 && bytes[1] == 0x00 && bytes[2] == 0x0F);
    }

    /* macOS:Ctrl + Cmd + Q = 修饰位 0x09 + 键码 0x14。 */
    {
        const pc_combo_t *c = pc_lock_combo(PC_OS_MACOS);
        assert(c != NULL);
        pc_kbd_clear(&r);
        assert(pc_kbd_add(&r, c->mods, c->keycode));
        memcpy(bytes, &r, sizeof(bytes));
        assert(bytes[0] == 0x09 && bytes[1] == 0x00 && bytes[2] == 0x14);
    }

    /* Linux:Super + L = 修饰位 0x08 + 键码 0x0F(与 Windows 同构)。 */
    {
        const pc_combo_t *c = pc_lock_combo(PC_OS_LINUX);
        assert(c != NULL);
        pc_kbd_clear(&r);
        assert(pc_kbd_add(&r, c->mods, c->keycode));
        memcpy(bytes, &r, sizeof(bytes));
        assert(bytes[0] == 0x08 && bytes[1] == 0x00 && bytes[2] == 0x0F);
    }

    /* ======== pc_consumer_pack:16 位 usage 小端 ======== */
    pc_consumer_pack(0xE9, consumer); /* PC_USAGE_VOL_UP */
    assert(consumer[0] == 0xE9);
    assert(consumer[1] == 0x00);

    pc_consumer_pack(0xCD, consumer); /* PC_USAGE_PLAY_PAUSE */
    assert(consumer[0] == 0xCD);
    assert(consumer[1] == 0x00);

    /* 高位非零的 usage 验证低字节在前(如 0x0240)。 */
    pc_consumer_pack(0x0240, consumer);
    assert(consumer[0] == 0x40);
    assert(consumer[1] == 0x02);

    /* ======== pc_hid_report_map:双报告描述符 ======== */

    /* NULL 缓冲仅查询长度;全长 > 0。 */
    map_len = pc_hid_report_map(NULL, 0);
    assert(map_len > 0);
    assert(map_len <= (int)sizeof(map));

    /* 实际写入与查询长度一致,且截断查询返回同值。 */
    assert(pc_hid_report_map(map, (int)sizeof(map)) == map_len);

    /* 描述符含两个 Report ID 项:HID "Report ID" 项前缀 0x85,
     * 键盘报告 ID = 0x01、Consumer 报告 ID = 0x02
     * (即 GATT Report Reference 描述符引用的两个 ID)。 */
    {
        int found_kbd = 0;
        int found_consumer = 0;
        for (i = 0; i + 1 < map_len; i++) {
            if (map[i] == 0x85 && map[i + 1] == PC_REPORT_ID_KEYBOARD) {
                found_kbd = 1;
            }
            if (map[i] == 0x85 && map[i + 1] == PC_REPORT_ID_CONSUMER) {
                found_consumer = 1;
            }
        }
        assert(found_kbd);
        assert(found_consumer);
        assert(PC_REPORT_ID_KEYBOARD == 1);
        assert(PC_REPORT_ID_CONSUMER == 2);
    }

    /* 描述符以两个 End Collection(0xC0)收尾:每个应用集合各一个,
     * 即恰好两个报告。 */
    {
        int collections = 0;
        for (i = 0; i < map_len; i++) {
            if (map[i] == 0xC0) {
                collections++;
            }
        }
        assert(collections == 2);
    }

    return 0;
}
