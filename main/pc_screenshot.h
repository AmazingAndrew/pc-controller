// main/pc_screenshot.h
// 串口截屏协议 (FAP_SCREENSHOT_V1):
//   - 通过 USB Serial/JTAG 输出 (避开 GPIO21 背光冲突,见 sdkconfig);
//   - 每块 8 行 × 240 × 2 = 3840 字节;
//   - 头: "FAP_SCREENSHOT_V1\n";
//   - 每块: "BLOCK <y_offset>,<height>,<data_len>\n" + 二进制数据;
//   - 尾: "END\n"。
//
// 用法: 组装层在菜单 (MENU_ITEM_SCREENSHOT) 确认后调用
// pc_screenshot_capture(); 屏幕内容会被一帧一帧推到 USB 串口。
// 协议解析器 (host 端) 收到 "FAP_SCREENSHOT_V1" 即开始解码。
#pragma once

#include <stdint.h>

/* 初始化模块 (占位,目前仅注册日志 tag)。 */
void pc_screenshot_start(void);

/* 触发一次截屏。截屏过程持有 bsp_lvgl_lock() 期间读取屏幕
 * 帧缓冲,输出在锁外进行 (usb_serial_jtag_write 不依赖 LVGL)。
 * 调用方应在持有 UI 锁的状态下调用,以保证 screen 句柄稳定;
 * 也可以在非 LVGL 上下文中调用 (内部会自行加锁)。
 *
 * 失败语义: 帧缓冲为空 / 内存分配失败 -> 输出 ERROR 行, 不崩溃。 */
void pc_screenshot_capture(void);