// main/pc_hid_reports.h
// PC Controller HID 报告层(平台无关纯逻辑模块)。
//
// 职责:
//   1. 提供 HOGP 用的 HID Report Map 描述符(双报告:标准 6KRO 键盘
//      + Consumer Page),描述符字节逐段注释;
//   2. 提供键盘报告的组帧原语(清零 / 追加修饰位与键码),
//      支持 6KRO 满键与重复键检测;
//   3. 提供 Consumer usage 的小端打包;
//   4. 集中定义本项目用到的全部 HID 键码 / 修饰位 / Consumer usage
//      常量,出处为 HID Usage Tables (Keyboard/Keypad Page 0x07、
//      Modifier Page 位定义、Consumer Page 0x0C)。
//
// 与 GATT 层的分工:每个报告带 Report ID,主机按 Report ID 路由;
// HOGP 要求的 Report Reference 描述符(类型 + ID)属于 GATT 特征
// 属性,由平台层 (BLE HID 组装)为每个 Report 特征声明,与本模块
// 的 Report ID 常量一一对应。
//
// 平台无关性:仅依赖 C11 标准库,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ---- Report ID(HOGP 多报告路由) ---- */

/* 键盘报告的 Report ID。HID Report Map 中用 0x85 项声明,
 * GATT 层 Report Reference 描述符使用同一 ID。 */
#define PC_REPORT_ID_KEYBOARD 1

/* Consumer Page 报告的 Report ID。 */
#define PC_REPORT_ID_CONSUMER 2

/* ---- HID 键码(HID Usage Tables, Keyboard/Keypad Page 0x07) ---- */

/* 方向键"上"(Keyboard UpArrow),用于下一页(规格 §1:UP = next page)。 */
#define PC_KEY_UP 0x52

/* 方向键"下"(Keyboard DownArrow),用于上一页。 */
#define PC_KEY_DOWN 0x51

/* F5(PowerPoint 等演示软件的"开始放映/进入全屏"键,规格 §1/FR-02)。 */
#define PC_KEY_F5 0x3E

/* Escape(退出全屏,与 F5 交替发送,规格 §1/FR-02)。 */
#define PC_KEY_ESC 0x29

/* 字母 L(Lock 组合键成员:Windows/Linux 的 Win/Super + L)。 */
#define PC_KEY_L 0x0F

/* 字母 Q(macOS 锁屏组合键成员:Ctrl + Cmd + Q,规格 §1/FR-03)。 */
#define PC_KEY_Q 0x14

/* 字母 B。保留常量:dev-plan 曾计划双击黑屏,确认基线已取消
 * (规格 §1 非目标第 1 条),保留键码仅为词汇完整,当前无绑定。 */
#define PC_KEY_B 0x05

/* ---- 修饰键位(键盘报告第 1 字节的位图,左修饰键) ---- */

/* LeftControl 位。 */
#define PC_MOD_LCTRL 0x01

/* LeftShift 位。 */
#define PC_MOD_LSHIFT 0x02

/* LeftAlt 位。 */
#define PC_MOD_LALT 0x04

/* LeftGUI 位(Windows 的 Win 键 / macOS 的 Cmd / Linux 的 Super)。 */
#define PC_MOD_LGUI 0x08

/* ---- Consumer Page usage(HID Usage Tables, Consumer Page 0x0C) ---- */

/* 音量 +(规格 §1/FR-04)。 */
#define PC_USAGE_VOL_UP 0xE9

/* 音量 -(规格 §1/FR-04)。 */
#define PC_USAGE_VOL_DOWN 0xEA

/* 播放/暂停(规格 §1/FR-04)。 */
#define PC_USAGE_PLAY_PAUSE 0xCD

/* ---- 报告结构 ---- */

/* 键盘报告负载(不含 Report ID 前缀;BLE HID 发送时由平台层
 * 按特征/引用附加 ID)。布局对应描述符声明的 8 字节:
 *   [0]    修饰位位图(见 PC_MOD_*)
 *   [1]    保留字节(OEM 用,恒 0)
 *   [2..7] 最多 6 个同时按下的键码(6KRO;超出以 ErrorRollOver
 *          语义处理——本模块的 pc_kbd_add 直接返回 false 拒绝)。 */
typedef struct {
    uint8_t mods;     /* 修饰位位图,多个修饰键按位或 */
    uint8_t reserved; /* 保留字节,恒 0,勿手工写入 */
    uint8_t keys[6];  /* 键码槽位,0 表示空槽 */
} pc_kbd_report_t;

/* 生成 HID Report Map 描述符。
 * 用途:平台层把返回值写入 HID Service 的 Report Map 特征。
 * 参数:
 *   buf 输出缓冲;允许 NULL(仅查询所需长度)。
 *   cap 缓冲容量;描述符只写入前 min(cap, 全长) 字节。
 * 返回值:描述符全长(字节)。若返回值 > cap,表示 buf 被截断,
 *        调用方应按返回值扩容后重试。
 * 失败值:无。
 * 线程上下文:任意;纯只读拷贝,无共享状态,无阻塞,无分配。
 * 内存所有权:只写调用方提供的 buf。 */
int pc_hid_report_map(uint8_t *buf, int cap);

/* 清零键盘报告(全部键释放)。每次"按下-释放"序列的开始与结束
 * 都应发送清零报告,避免主机侧卡键(规格 §1/FR-01)。
 * 参数:r 报告实例,不可为 NULL(为 NULL 时直接返回)。
 * 线程上下文:任意;无阻塞。内存所有权:写调用方对象。 */
void pc_kbd_clear(pc_kbd_report_t *r);

/* 向键盘报告追加一组"修饰位 + 键码"。
 * 参数:
 *   r       报告实例,不可为 NULL。
 *   mods    本次追加的修饰位(与既有修饰位按位或,不清除旧位)。
 *   keycode 键码;0 视为非法(0 在报告语义中是空槽/错误占位)。
 * 返回值:
 *   true  追加成功;
 *   false 追加失败——键码为 0、与既有键码重复、或 6 个槽位已满。
 *         失败时报告不被修改(修饰位也不并入),调用方应按
 *         ErrorRollOver 语义决定降级策略。
 * 线程上下文:任意;无阻塞。内存所有权:写调用方对象。 */
bool pc_kbd_add(pc_kbd_report_t *r, uint8_t mods, uint8_t keycode);

/* 把 16 位 Consumer usage 打包为 2 字节小端字节流,
 * 对应 Consumer 报告的 16 位 Data 字段。
 * 参数:
 *   usage 16 位 usage(如 PC_USAGE_VOL_UP);
 *   out   2 字节输出,out[0] 为低字节。不可为 NULL(为 NULL 时
 *         直接返回)。
 * 线程上下文:任意;无阻塞。内存所有权:写调用方 out。 */
void pc_consumer_pack(uint16_t usage, uint8_t out[2]);
