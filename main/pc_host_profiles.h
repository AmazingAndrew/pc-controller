// main/pc_host_profiles.h
// PC Controller 主机配置表(平台无关纯逻辑模块)。
//
// 职责:数据驱动的"操作系统 -> 锁屏组合键"映射表(规格 §1/FR-03、
// §11 扩展性)。每个槽位记录其主机配置的 OS 类型(存于 NVS,
// 规格 §8),锁屏时组装层按该类型从本表取组合键发送。
//
// 锁屏组合键事实源(规格 §1/FR-03):
//   Windows = Win + L
//   macOS   = Ctrl + Cmd + Q
//   Linux   = Super + L(主流桌面环境通用)
//
// 平台无关性:仅依赖 C11 标准库与本目录的纯逻辑头,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pc_hid_reports.h" /* pc_combo_t 依赖其键码/修饰位常量语义 */

/* 主机操作系统类型。槽位元数据中持久化该枚举值(规格 §8
 * `pp_slot*` 命名空间的 "OS type" 字段)。
 * 新增 OS 时在 PC_OS_MAX 之前插入即可,三个查询接口自动适配
 * (规格 §11:数据驱动可扩展)。 */
typedef enum {
    PC_OS_WINDOWS = 0,
    PC_OS_MACOS,
    PC_OS_LINUX,
    PC_OS_MAX /* 数量哨兵,不表示真实 OS */
} pc_os_t;

/* 一个"修饰位 + 键码"组合键成员。锁屏等组合键由若干
 * (修饰位, 键码)对组成;当前三个 OS 的锁屏组合都是
 * "一个修饰位 + 一个键"形态,故表项为单项。 */
typedef struct {
    uint8_t mods;    /* 修饰位组合(见 pc_hid_reports.h 的 PC_MOD_*) */
    uint8_t keycode; /* 键码(见 pc_hid_reports.h 的 PC_KEY_*) */
} pc_combo_t;

/* 取某 OS 的锁屏组合键。
 * 参数:os 主机操作系统类型。
 * 返回值:指向静态只读表项的指针,生命周期为整个程序运行期;
 *        Windows -> LGUI + L(0x08, 0x0F);
 *        macOS   -> (LCTRL | LGUI) + Q(0x09, 0x14);
 *        Linux   -> LGUI + L(与 Windows 同构,键位语义不同)。
 * 失败值:os 越界(>= PC_OS_MAX)返回 NULL;调用方必须判空,
 *        未知配置降级为"不发送"(宁可不锁屏也不发错组合,
 *        规格 §1/FR-03:不得向不同配置的主机发错组合键)。
 * 线程上下文:任意;返回只读静态数据,无阻塞,无分配。
 * 内存所有权:返回指针指向静态存储,调用方不得释放或写入。 */
const pc_combo_t *pc_lock_combo(pc_os_t os);

/* 取某 OS 的配置档案名(用于菜单/屏显/日志)。
 * 返回值:"WINDOWS" / "MACOS" / "LINUX"(大写,纯 ASCII,
 *        与屏显字符串表的字符集约束一致)。
 * 失败值:os 越界返回 NULL。
 * 线程上下文/内存所有权:同 pc_lock_combo。 */
const char *pc_profile_name(pc_os_t os);

/* 取某 OS 锁屏组合键的人类可读文案(屏显提示用)。
 * 返回值:"WIN+L" / "CTRL+CMD+Q" / "SUPER+L"。
 * 失败值:os 越界返回 NULL。
 * 线程上下文/内存所有权:同 pc_lock_combo。 */
const char *pc_combo_text(pc_os_t os);
