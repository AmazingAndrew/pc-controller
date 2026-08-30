// main/pc_hid_reports.c
// HID Report Map 描述符与报告组帧原语实现。
//
// 描述符依据:
//   - Device Class Definition for HID 1.11(标准键盘描述符模板);
//   - HID Usage Tables for Universal Serial Bus(Keyboard/Keypad
//     Page 0x07、Consumer Page 0x0C);
//   - HOGP 1.0(Report ID + Report Reference 多报告路由)。
//
// 平台无关:仅依赖 C11 标准库,可被 host 测试直接编译。
#include "pc_hid_reports.h"

#include <string.h>

/* HID Report Map:两个应用集合,各带一个 Report ID。
 * 字节序即主机解析顺序;每个条目后以行内注释逐段解释。
 * 长度用 sizeof 自动跟踪,增删条目无需手改魔数。 */
static const uint8_t s_report_map[] = {
    /* ===================== 报告 1:标准键盘(6KRO) ===================== */
    0x05, 0x01,                    /* Usage Page (Generic Desktop, 0x01) */
    0x09, 0x06,                    /* Usage (Keyboard) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, PC_REPORT_ID_KEYBOARD,   /* Report ID (1):HOGP 多报告路由 */
    /* --- 第 1 字节:8 个修饰键,每键 1 位 --- */
    0x05, 0x07,                    /* Usage Page (Keyboard/Keypad, 0x07) */
    0x19, 0xE0,                    /* Usage Minimum (0xE0 = LeftControl) */
    0x29, 0xE7,                    /* Usage Maximum (0xE7 = Right GUI) */
    0x15, 0x00,                    /* Logical Minimum (0) */
    0x25, 0x01,                    /* Logical Maximum (1) */
    0x75, 0x01,                    /* Report Size (1 bit) */
    0x95, 0x08,                    /* Report Count (8) -> 1 个修饰位字节 */
    0x81, 0x02,                    /* Input (Data, Variable, Absolute) */
    /* --- 第 2 字节:厂商保留字节,恒 0 --- */
    0x95, 0x01,                    /* Report Count (1) */
    0x75, 0x08,                    /* Report Size (8 bit) */
    0x81, 0x01,                    /* Input (Constant):保留字节 */
    /* --- 第 3..8 字节:6 个并发键码槽位(6KRO) --- */
    0x95, 0x06,                    /* Report Count (6) */
    0x75, 0x08,                    /* Report Size (8 bit) */
    0x15, 0x00,                    /* Logical Minimum (0) */
    0x26, 0xFF, 0x00,              /* Logical Maximum (255,两字节编码) */
    0x19, 0x00,                    /* Usage Minimum (0) */
    0x29, 0xFF,                    /* Usage Maximum (255,覆盖全部键码) */
    0x81, 0x00,                    /* Input (Data, Array):键码数组 */
    0xC0,                          /* End Collection */

    /* ================= 报告 2:Consumer Page(媒体控制) ================= */
    0x05, 0x0C,                    /* Usage Page (Consumer Devices, 0x0C) */
    0x09, 0x01,                    /* Usage (Consumer Control) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, PC_REPORT_ID_CONSUMER,   /* Report ID (2):HOGP 多报告路由 */
    0x15, 0x00,                    /* Logical Minimum (0) */
    0x26, 0xFF, 0x03,              /* Logical Maximum (1023,两字节编码) */
    0x19, 0x00,                    /* Usage Minimum (0) */
    0x2A, 0xFF, 0x03,              /* Usage Maximum (1023,两字节编码) */
    0x75, 0x10,                    /* Report Size (16 bit) */
    0x95, 0x01,                    /* Report Count (1):一个 16 位 usage */
    0x81, 0x00,                    /* Input (Data, Array) */
    0xC0,                          /* End Collection */
};

int pc_hid_report_map(uint8_t *buf, int cap)
{
    const int len = (int)sizeof(s_report_map);

    /* buf 允许为 NULL(仅查询长度);cap <= 0 时同样只返回全长。 */
    if (buf != NULL && cap > 0) {
        /* 只写入缓冲能容纳的部分,截断时不越界。 */
        memcpy(buf, s_report_map, (size_t)(cap < len ? cap : len));
    }
    return len;
}

void pc_kbd_clear(pc_kbd_report_t *r)
{
    if (r == NULL) {
        return;
    }
    /* 整体清零:修饰位、保留字节与 6 个键槽全部归零,
     * 即"所有键释放"报告。 */
    r->mods = 0U;
    r->reserved = 0U;
    memset(r->keys, 0, sizeof(r->keys));
}

bool pc_kbd_add(pc_kbd_report_t *r, uint8_t mods, uint8_t keycode)
{
    int free_idx = -1;
    int i;

    if (r == NULL || keycode == 0U) {
        /* 键码 0 在报告中表示空槽/错误占位,拒绝作为真实键追加。 */
        return false;
    }

    /* 一遍扫描:查重 + 找第一个空槽。重复键不重复占槽
     * (HID 语义:同一物理键按下多次只算一次)。 */
    for (i = 0; i < (int)(sizeof(r->keys) / sizeof(r->keys[0])); i++) {
        if (r->keys[i] == keycode) {
            return false; /* 与既有键码重复 */
        }
        if (r->keys[i] == 0U && free_idx < 0) {
            free_idx = i;
        }
    }

    if (free_idx < 0) {
        /* 6KRO 满键:按 ErrorRollOver 语义拒绝,由调用方降级。 */
        return false;
    }

    /* 全部校验通过后才修改报告,保证失败时报告保持原样。 */
    r->mods |= mods;
    r->keys[free_idx] = keycode;
    return true;
}

void pc_consumer_pack(uint16_t usage, uint8_t out[2])
{
    if (out == NULL) {
        return;
    }
    /* 小端:低字节在前。与描述符声明的 16 位 Data 字段字节序一致
     * (HID 报告数据一律小端)。 */
    out[0] = (uint8_t)(usage & 0xFFU);
    out[1] = (uint8_t)((usage >> 8) & 0xFFU);
}
