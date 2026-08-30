// main/pc_ble_hid.h
// PC Controller 平台层:BLE HID(HOGP)模块接口。
//
// 职责:把纯逻辑层的报告定义 (pc_hid_reports.h) 接到 NimBLE HOGP
// GATT/GAP 服务上,对组装层只暴露"启停 / 广播 / 发送 / 事件回调"
// 六类接口。事实源:
//   - requirements.md §2(HID 0x1812 + DIS 0x180A + BAS 0x180F、
//     双报告:6KRO 键盘 + Consumer Page、MAX_CONNECTIONS=1);
//   - requirements.md §5(BLE 输入:connect/disconnect/subscribe/
//     passkey/supervision timeout;输出:keyboard/consumer 报告、
//     广播控制);
//   - requirements.md §13(配对策略与已知风险)。
//
// 线程模型(规格 §7 行 161):
//   - NimBLE host 任务独占 GAP/GATT 处理;GAP/SM 回调只通过
//     用户回调向上透传归一事件,不做重活;
//   - HID 报告发送由应用侧(组装层的应用任务)直接调用本模块
//     的 send 接口,接口内部走 NimBLE API 投递,非阻塞。
//
// 启停骨架与 main/demo_ble.c 同构(前置 NVS -> nimble_port_init ->
// GATT/GAP 服务 -> ble_hs_cfg -> nimble_port_freertos_init;停止用
// nimble_port_stop + 信号量等待),差异仅在服务表与安全配置。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pc_hid_reports.h" /* pc_kbd_report_t 与 Report ID 常量 */

/* 归一后的 BLE 侧事件。由本模块在 NimBLE 回调里产生,经用户回调
 * 上报;组装层再把其中四类映射到纯逻辑状态机的 pc_ble_evt_t
 * (pc_app_fsm.h),PASSKEY 单独走屏显通道。 */
typedef enum {
    PC_BLE_EVT_CONNECTED = 0,    /* 连接建立(参数:无) */
    PC_BLE_EVT_DISCONNECTED,     /* 连接断开(含监督超时,规格 §10) */
    PC_BLE_EVT_PAIR_OK,          /* 配对/绑定成功(加密建立) */
    PC_BLE_EVT_PAIR_FAIL,        /* 配对失败(配对码输错/主机拒绝) */
    PC_BLE_EVT_PASSKEY,          /* 需要屏显 6 位配对码;arg = 配对码
                                  * (100000..999999,规格 §1/FR-07) */
} pc_ble_hid_evt_t;

/* 初始化并启动 NimBLE host(HOGP 服务表 + 配对配置)。
 * 前置:依赖 NVS 已就绪(组装层先调 pc_storage_init();本模块内部
 *       再做一次幂等的"失败不擦除"准备,与 demo_radio_nvs_prepare()
 *       同模式——注意:presenter 档不编译 demo_radio.c,故该模式在
 *       本模块内复刻,绝不擦分区)。
 * 行为:注册 GATT 服务表 -> 配置设备名 "PC-CTRL"(规格配对页文案,
 *       ui-design §4.3)与 GAP Appearance 0x03C1(通用 HID)->
 *       配置安全参数(DISPLAY_ONLY + SC + MITM + bonding)->
 *       nimble_port_freertos_init 拉起 host 任务。
 * 参数:
 *   cb   事件回调,不可为 NULL。运行于 NimBLE host 任务上下文,
 *        实现必须只做入队之类的轻活(规格 §7)。
 *   user 透传给回调的用户指针。
 * 返回值:ESP_OK 成功。
 * 失败值:
 *   ESP_ERR_INVALID_ARG   cb 为 NULL;
 *   ESP_ERR_INVALID_STATE 重复初始化;
 *   其余                  底层错误透传(此时无线功能不可用,
 *                         组装层按规格 §10 降级)。
 * 线程上下文:应用任务启动阶段调用一次。 */
esp_err_t pc_ble_hid_init(void (*cb)(pc_ble_hid_evt_t, uint32_t arg, void *user), void *user);

/* 启动通用可发现广播(配对用:任意主机可扫描到 "PC-CTRL")。
 * 广播数据含完整设备名、Appearance 与 16 位服务 UUID 列表。
 * 返回值:ESP_OK;未初始化/底层失败返回错误码(组装层屏显降级)。
 * 线程上下文:应用任务;非阻塞。 */
esp_err_t pc_ble_hid_start_adv_general(void);

/* 启动对指定地址的定向广播(槽位回连/重配对,规格 §10:
 * 断连后先定向 30 s)。
 * 参数:addr 6 字节对端身份地址(槽位元数据中的绑定地址)。
 * 地址类型说明:定向广播需要地址类型;槽位元数据只持久化 6 字节
 * 地址(规格 §8),故本模块优先使用最近一次连接记录到的对端身份
 * 地址类型,无记录时按 BLE_ADDR_PUBLIC 处理——已知局限,见 .c 注释。
 * 返回值:同上。 */
esp_err_t pc_ble_hid_start_adv_directed(const uint8_t addr[6]);

/* 停止广播(退出配对页/配对成功后调用;连接建立后广播自动停止)。
 * 返回值:ESP_OK;未初始化返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pc_ble_hid_stop_adv(void);

/* 是否存在活动连接。
 * 线程上下文:任意(只读原子标志);无阻塞。 */
bool pc_ble_hid_connected(void);

/* 发送一帧键盘报告,并必跟一帧空报告释放按键(防卡键,
 * 规格 §1/FR-01:每次按键后主机侧无残留修饰位/键码)。
 * 参数:r 待发送报告;为 NULL 时返回错误码不发送。
 * 返回值:
 *   ESP_OK                  两帧(按下 + 释放)均投递成功;
 *   ESP_ERR_INVALID_STATE   未连接,或主机已下 HOGP Suspend;
 *   其余                    底层投递失败,组装层据此降级(屏显链路丢失)。
 * 线程上下文:应用任务;非阻塞。 */
esp_err_t pc_ble_hid_send_keyboard(const pc_kbd_report_t *r);

/* 发送一次 Consumer Page 按键:press(usage)+ release(0x0000)两帧。
 * 参数:usage 16 位 usage(PC_USAGE_VOL_UP / VOL_DOWN / PLAY_PAUSE)。
 * 返回值:同 pc_ble_hid_send_keyboard。 */
esp_err_t pc_ble_hid_send_consumer(uint16_t usage);

/* 通过 BAS 0x180F 的 Battery Level 特征 notify 电量。
 * 参数:percent 0..100。读数为 -1 时组装层不得调用本接口
 *       (规格 §10:Battery Service 停止通知)。
 * 返回值:未连接返回 ESP_ERR_INVALID_STATE;底层失败透传。
 * 线程上下文:应用任务;非阻塞。 */
esp_err_t pc_ble_hid_battery_notify(uint8_t percent);

/* 优雅断开当前连接(槽位切换前置动作,规格 §1/FR-06)。
 * 断开后设备侧停止一切 HID 发送,按 HOGP 语义进入 suspend 态
 * (FR-06:switch first gracefully disconnects ... the device enters
 * the suspend state per the HOGP specification);随后由组装层对
 * 目标槽发起定向广播。
 * 返回值:
 *   ESP_OK                断开请求已提交(断开事件经回调上报);
 *   ESP_ERR_INVALID_STATE 当前本就无连接(视为成功路径,不报错语义
 *                         由组装层决定;此处返回该码仅供日志)。
 * 线程上下文:应用任务;非阻塞。 */
esp_err_t pc_ble_hid_graceful_disconnect(void);

/* 取最近一次成功连接的对端身份地址(供组装层在配对成功时写入
 * 槽位元数据,规格 §8 `pp_slot*` 的 bond address 字段)。
 * 参数:out 6 字节输出缓冲,不可为 NULL。
 * 返回值:
 *   ESP_OK                已拷贝;
 *   ESP_ERR_INVALID_STATE 尚无任何连接记录。
 * 线程上下文:应用任务;无阻塞。 */
esp_err_t pc_ble_hid_peer_addr(uint8_t out[6]);

/* 停止整个 BLE 栈(优雅断连 -> 停广播 -> nimble_port_stop +
 * 信号量等待 -> deinit,与 demo_ble.c 的停止路径同构)。
 * 阻塞性:等待 host 任务退出,典型 < 100 ms;仅用于深睡前/
 * 关机路径(电源任务,后续里程碑接入),日常运行不调用。
 * 返回值:ESP_OK;未初始化返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pc_ble_hid_stop(void);
