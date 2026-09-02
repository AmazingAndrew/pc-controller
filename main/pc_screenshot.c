// main/pc_screenshot.c
// 串口截屏协议 (FAP_SCREENSHOT_V1) 实现。
//
// 协议 (供 host 端解析器参考):
//   "FAP_SCREENSHOT_V1\n"
//   "W <w>,H <h>,FMT <fmt>\n"
//   "BLOCK <y>,<h>,<bytes>\n" + <bytes 二进制数据>
//   ...
//   "END\n"
//
// 实现要点:
//   - 调用 LVGL 的 lv_snapshot_take() 抓取当前 active screen;
//     抓取过程在 LVGL 任务上下文进行 (内部 bsp_lvgl_lock),
//     抓取后立即释放锁再进行 USB 写出 (避免长时持有 UI 锁,
//     USB 写出按 240×320×2 = 153 KB 全帧可能阻塞);
//   - 按 8 行分块 (240×8×2 = 3840 字节 / 块), 逐块写出,
//     主机侧解析器可流式解码;
//   - 写出失败仅记日志, 不重试 (单帧截屏一次性服务)。
//
// 内存约束: ESP32-C3 无 PSRAM, 全帧 153 KB 抓取需要 LVGL 内存池
// 至少能分配该尺寸的 draw_buf。当前 LVGL_MEM_SIZE_KILOBYTES=32
// (见 sdkconfig.defaults); 24 KB 演示档可通过 (24-32 KB 池), 大块
// 抓取依然有失败风险。失败时输出 "ERROR <reason>" 行, 解析器
// 据此跳过本帧。

#include "pc_screenshot.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_display.h"

#include "driver/usb_serial_jtag.h"
#include "lvgl.h"

static const char *TAG = "pc_screenshot";

/* 屏参数 (与 pc_ui_fui.h 一致; 此处独立定义避免循环依赖)。 */
#define PC_SCREEN_W 240
#define PC_SCREEN_H 320

/* 分块高度: 8 行 = 240×8×2 = 3840 字节 / 块。 */
#define PC_BLOCK_ROWS 8

/* 写出节流: 每块之间让出 CPU, 避免 USB FIFO 阻塞导致看门狗。 */
#define PC_BLOCK_YIELD_MS 5

void pc_screenshot_start(void)
{
    /* 当前实现无模块级状态需初始化; 保留入口以兼容未来
     * (例如打开 USB CDC、握手)。 */
    ESP_LOGI(TAG, "screenshot module ready (FAP_SCREENSHOT_V1)");
}

/* ---- 内部: USB 串口写出助手 ---- */

static void write_line(const char *line)
{
    if (line == NULL) return;
    /* usb_serial_jtag_write 返回写入字节数, 失败返回负数; 截屏场景
     * 不重试, 仅记录日志。 */
    int n = usb_serial_jtag_write_bytes(line, strlen(line), 0);
    if (n < 0) {
        ESP_LOGW(TAG, "usb write failed: %d", n);
    }
}

/* 写出定长二进制数据 (USB CDC 通道不依赖 '\n' 分隔)。 */
static void write_bytes(const void *buf, size_t len)
{
    if (buf == NULL || len == 0) return;
    int n = usb_serial_jtag_write_bytes(buf, len, 0);
    if (n < 0) {
        ESP_LOGW(TAG, "usb write bytes failed: %d", n);
    }
}

/* ---- 内部: 抓取当前屏帧 ---- */

/* 抓取当前 active screen 到 RGB565 格式的 draw_buf。
 * 返回: 成功返回 lv_draw_buf_t* (调用方负责 lv_draw_buf_destroy);
 *       失败返回 NULL。调用方已持 bsp_lvgl_lock()。 */
static lv_draw_buf_t *take_snapshot(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (scr == NULL) {
        ESP_LOGW(TAG, "no active screen");
        return NULL;
    }
    /* RGB565 与屏实际格式匹配 (sdkconfig LV_COLOR_DEPTH_16=y);
     * snapshot 直接落到 RGB565 避免再做格式转换。 */
    lv_draw_buf_t *buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    if (buf == NULL) {
        ESP_LOGW(TAG, "snapshot alloc failed (LVGL pool likely exhausted)");
        return NULL;
    }
    return buf;
}

/* 把 buf 按 8 行分块写出。buf 行宽 = buf->header.stride,
 * 数据起始 = buf->data, 每块 8×stride 字节。 */
static void emit_blocks(const lv_draw_buf_t *buf)
{
    const uint32_t w = buf->header.w;
    const uint32_t h = buf->header.h;
    const uint32_t stride = buf->header.stride;
    const uint8_t *base = buf->data;

    char hdr[64];
    for (uint32_t y = 0; y < h; y += PC_BLOCK_ROWS) {
        const uint32_t rows = (y + PC_BLOCK_ROWS <= h) ?
                              PC_BLOCK_ROWS : (h - y);
        const uint32_t bytes = rows * stride;
        snprintf(hdr, sizeof(hdr), "BLOCK %u,%u,%u\n",
                 (unsigned)y, (unsigned)rows, (unsigned)bytes);
        write_line(hdr);
        write_bytes(base + (size_t)y * stride, bytes);
        /* 块间让出 CPU, 防止 USB FIFO 阻塞导致 LVGL 任务饿死。 */
        vTaskDelay(pdMS_TO_TICKS(PC_BLOCK_YIELD_MS));
    }
}

/* ---- 公开接口 ---- */

void pc_screenshot_capture(void)
{
    ESP_LOGI(TAG, "capture begin");

    /* 头 + 屏参数 (先在锁外写小行, 不阻塞)。 */
    write_line("FAP_SCREENSHOT_V1\n");
    char info[48];
    snprintf(info, sizeof(info), "W %u,H %u,FMT RGB565\n",
             (unsigned)PC_SCREEN_W, (unsigned)PC_SCREEN_H);
    write_line(info);

    lv_draw_buf_t *buf = NULL;
    bool locked = bsp_lvgl_lock(200);
    if (!locked) {
        write_line("ERROR lock_timeout\n");
        write_line("END\n");
        ESP_LOGW(TAG, "capture abort: lock timeout");
        return;
    }
    buf = take_snapshot();
    bsp_lvgl_unlock();

    if (buf == NULL) {
        write_line("ERROR snapshot_failed\n");
        write_line("END\n");
        ESP_LOGW(TAG, "capture abort: snapshot failed");
        return;
    }

    /* 块写出 (锁外; USB 写出与 LVGL 解耦)。 */
    emit_blocks(buf);

    /* 立即释放 draw_buf 内存 (规格 §9: 不在帧期间残留大缓冲)。 */
    lv_draw_buf_destroy(buf);

    write_line("END\n");
    ESP_LOGI(TAG, "capture done");
}