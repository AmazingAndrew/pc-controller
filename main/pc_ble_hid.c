// main/pc_ble_hid.c —— PC Controller 平台层:NimBLE HOGP 实现。
//
// 启停骨架照搬 main/demo_ble.c(行 98-172 的启动/停止路径、行 79-88
// 的 on_sync),服务与安全配置按规格 §2/§5/§13 定制。事实源引用
// 逐条写在各段注释中。
//
// 已知局限(记录在案,不阻塞本里程碑):
//   1. 定向广播的地址类型取自最近一次连接记录(规格 §8 槽位元数据
//      只持久化 6 字节地址,未含类型);无记录时按 public 处理。
//   2. DISPLAY_ONLY + MITM 请求下,部分主机仍会退化为 Just Works
//      (规格 §13 已知风险):此时无配对码可显示,配对仍成立,
//      只是失去 MITM 保护,真机验收时需逐主机确认。
//   3. NimBLE 的定向广播为高占空比模式,单轮最长约 1.28 s,到期
//      后由本文件的自动续发逻辑重发——30 s 定向窗口即"连续续发",
//      语义与规格 §10 的"定向广播 30 s"一致。
#include "pc_ble_hid.h"

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

static const char *TAG = "pc_ble_hid";

/* 设备名:规格配对页要求主机侧出现 "PC-CTRL"(ui-design §4.3
 * 的 `SELECT "PC-CTRL" ON HOST` 提示行)。 */
#define PC_DEVICE_NAME "PC-CTRL"

/* GAP Appearance 0x03C1 = Generic HID(HID over GATT 规范)。 */
#define PC_APPEARANCE_GENERIC_HID 0x03C1

/* HID Control Point 写入值(HOGP §6.5)。 */
#define PC_HID_CP_SUSPEND 0x00
#define PC_HID_CP_EXIT_SUSPEND 0x01

/* ---- Vendor Service 预留占位(规格 §11) ----
 * 伴机 (companion) 功能的 128-bit Vendor Service UUID 预留位。
 * 当前未启用:不注册进下方服务表,避免主机看到一个空服务。
 * 启用时把 #if 0 改为 #if 1 并补特征即可,应用侧零改动。 */
#if 0
#define PC_VENDOR_SVC_UUID128                                                     \
    {                                                                             \
        .u = { .type = BLE_UUID_TYPE_128 },                                       \
        .u128.value = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,          \
                        0x08, 0x09, 0x0A, 0x0B, 0x50, 0x43, 0x2D, 0xC7 }         \
    }
#endif

/* ---- 文件内状态(s_ 前缀,编码约定) ---- */

static void (*s_evt_cb)(pc_ble_hid_evt_t, uint32_t arg, void *user);
static void *s_evt_user;

static SemaphoreHandle_t s_host_stopped; /* host 任务退出信号量,停止路径等待 */
static bool s_initialized;               /* nimble_port_init 已完成 */
static bool s_connected;                 /* 存在活动连接 */
static bool s_host_suspended;            /* 主机下过 HOGP Suspend,禁止发送 */
static bool s_auto_readvertise;          /* 广播到期自动续发(配对/回连窗口) */
static bool s_adv_directed;              /* 当前续发模式:定向(否则通用) */
static uint8_t s_adv_direct_addr[6];     /* 定向续发目标地址 */
static uint8_t s_addr_type;              /* 本机地址类型(广播用) */
static uint16_t s_conn_handle;           /* 当前连接句柄 */

/* 最近一次连接的对端身份地址与类型:配对成功时组装层取走写槽位;
 * 定向广播在无记录时也借用其类型(局限 1)。 */
static bool s_peer_known;
static uint8_t s_peer_addr[6];
static uint8_t s_peer_addr_type;

/* GATT 特征句柄:notify 需要。 */
static uint16_t s_kbd_report_handle;
static uint16_t s_consumer_report_handle;
static uint16_t s_battery_handle;

/* Report Map 描述符:启动时从纯逻辑层一次性取到静态缓冲,
 * GATT 读取回调直接引用,无运行期分配。 */
static uint8_t s_report_map[128];
static int s_report_map_len;

/* 电量读数缓存:Battery Level 特征读回调用;notify 前更新。 */
static uint8_t s_battery_percent;

/* 最近一帧报告缓存:主机读 Report 特征时返回(输入报告读语义
 * 罕见,返回最近值即可)。 */
static uint8_t s_last_kbd[8];     /* mods + reserved + 6 keys */
static uint8_t s_last_consumer[2]; /* 16 位 usage 小端 */

/* HID Protocol Mode 特征当前值;1 = Report Protocol(上电默认)。 */
static uint8_t s_protocol_mode = 1;

/* HID Information 特征值(HOGP §5.4,共 4 字节):
 *   bcdHID  = 0x0111 (HID 1.11,小端)
 *   country = 0x00(不本地化)
 *   flags   = 0x00(不声明 normally-connectable / remote-wake;
 *             唤醒与回连策略走广播占空比,规格 §10) */
static const uint8_t s_hid_info[4] = { 0x11, 0x01, 0x00, 0x00 };

static int gap_event(struct ble_gap_event *event, void *arg);
static void on_sync(void);
static void on_reset(int reason);

/* 事件上报助手:只做一次函数指针调用,不阻塞、不分配。 */
static void emit(pc_ble_hid_evt_t ev, uint32_t arg)
{
    if (s_evt_cb) s_evt_cb(ev, arg, s_evt_user);
}

/* NVS 前置准备:与 demo_radio_nvs_prepare()(demo_radio.c 行 14-26)
 * 同模式——初始化失败仅记日志,绝不擦分区(分区里可能有未来应用
 * 已保存的数据,也可能是同分区共存的其它命名空间)。
 * 重复一份而非直接调用,是因为 presenter 档的 main/CMakeLists.txt
 * 不编译 demo_radio.c(presenter 分支只注册 pc_*.c)。 */
static esp_err_t nvs_prepare_no_erase(void)
{
    static bool nvs_ready;
    if (nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s; partition NOT erased", esp_err_to_name(err));
        return err;
    }
    nvs_ready = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* GATT 访问回调                                                       */
/* ------------------------------------------------------------------ */

/* Report Map(0x2A4B):只读,内容来自纯逻辑层 pc_hid_report_map()。 */
static int cb_report_map(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_report_map, s_report_map_len) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* HID Information(0x2A4A):只读 4 字节。 */
static int cb_hid_info(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_hid_info, sizeof(s_hid_info)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* HID Control Point(0x2A4C):主机写 0 = Suspend、1 = Exit Suspend。
 * 设备侧据此开关发送通道(规格 §5 输出受主机电源策略约束);
 * 不做其它动作(无唤醒源需要驱动)。 */
static int cb_hid_control(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t cmd = 0xFF;
    if (OS_MBUF_PKTLEN(ctxt->om) >= 1) {
        ble_hs_mbuf_to_flat(ctxt->om, 0, &cmd, 1, NULL);
    }
    if (cmd == PC_HID_CP_SUSPEND) {
        s_host_suspended = true;
        ESP_LOGI(TAG, "HOGP: host requested Suspend");
    } else if (cmd == PC_HID_CP_EXIT_SUSPEND) {
        s_host_suspended = false;
        ESP_LOGI(TAG, "HOGP: host requested Exit Suspend");
    }
    return 0; /* write-no-response:接受即可 */
}

/* Protocol Mode(0x2A4E):读返回当前模式;写仅接受(本设备只实现
 * Report Protocol,对 Boot Protocol 切换请求静默接受不生效)。 */
static int cb_protocol_mode(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, &s_protocol_mode, 1) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

/* Report 特征(0x2A4D,键盘):读返回最近一帧;写接受不生效
 * (输入报告的主机写路径本设备无消费方)。 */
static int cb_kbd_report(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_last_kbd, sizeof(s_last_kbd)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

/* Report 特征(0x2A4D,Consumer):同上。 */
static int cb_consumer_report(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_last_consumer, sizeof(s_last_consumer)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

/* Report Reference(0x2908)描述符:2 字节 = [Report ID][Report Type]。
 * Type 1 = Input Report(HOGP §5.2)。arg 携带 Report ID。 */
static int cb_report_ref(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
    uint8_t v[2] = { (uint8_t)(uintptr_t)arg, 0x01 };
    return os_mbuf_append(ctxt->om, v, sizeof(v)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* Battery Level(0x2A19):读返回缓存电量;notify 由电池轮询触发。 */
static int cb_battery(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, &s_battery_percent, 1) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* DIS 字符串特征(0x2A29 厂商名 / 0x2A50 产品名):只读。
 * 产品名按规格固定为 "PC Controller";厂商名取仓库品牌。 */
static int cb_dis_string(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    const char *s = (const char *)arg;
    return os_mbuf_append(ctxt->om, s, (uint16_t)strlen(s)) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ------------------------------------------------------------------ */
/* GATT 服务表                                                         */
/* ------------------------------------------------------------------
 * 逐项与规格对照:
 *   HID Service 0x1812(规格 §2):
 *     Report Map 0x2A4B      内容来自 pc_hid_report_map()(双报告)
 *     HID Information 0x2A4A
 *     HID Control Point 0x2A4C
 *     Protocol Mode 0x2A4E
 *     Report 0x2A4D(键盘,ID 1)+ Report Reference 0x2908
 *     Report 0x2A4D(Consumer,ID 2)+ Report Reference 0x2908
 *   DIS 0x180A:厂商名 + 产品名 "PC Controller"
 *   BAS 0x180F:Battery Level 0x2A19(read + notify,规格 §1/FR-11)
 *   GAP Appearance 0x03C1:在初始化路径经
 *     ble_svc_gap_device_appearance_set() 设置,不落本表。
 *   Vendor Service:规格 §11 预留 128-bit UUID 占位,未启用,
 *     见文件头 #if 0 区块。 */
static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812), /* HID Service */
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Report Map:只读 */
                .uuid = BLE_UUID16_DECLARE(0x2A4B),
                .access_cb = cb_report_map,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* HID Information:只读 */
                .uuid = BLE_UUID16_DECLARE(0x2A4A),
                .access_cb = cb_hid_info,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* HID Control Point:write-no-response */
                .uuid = BLE_UUID16_DECLARE(0x2A4C),
                .access_cb = cb_hid_control,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* Protocol Mode:读 + write-no-response */
                .uuid = BLE_UUID16_DECLARE(0x2A4E),
                .access_cb = cb_protocol_mode,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* Report(键盘,Report ID = PC_REPORT_ID_KEYBOARD) */
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = cb_kbd_report,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_kbd_report_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), /* Report Reference */
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = cb_report_ref,
                        .arg = (void *)(uintptr_t)PC_REPORT_ID_KEYBOARD,
                    },
                    { 0 },
                },
            },
            {
                /* Report(Consumer,Report ID = PC_REPORT_ID_CONSUMER) */
                .uuid = BLE_UUID16_DECLARE(0x2A4D),
                .access_cb = cb_consumer_report,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_consumer_report_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), /* Report Reference */
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = cb_report_ref,
                        .arg = (void *)(uintptr_t)PC_REPORT_ID_CONSUMER,
                    },
                    { 0 },
                },
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A), /* Device Information Service */
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Manufacturer Name String 0x2A29 */
                .uuid = BLE_UUID16_DECLARE(0x2A29),
                .access_cb = cb_dis_string,
                .arg = (void *)"FoloToy",
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* Product Name String 0x2A50(规格:"PC Controller") */
                .uuid = BLE_UUID16_DECLARE(0x2A50),
                .access_cb = cb_dis_string,
                .arg = (void *)"PC Controller",
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F), /* Battery Service */
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Battery Level 0x2A19:读 + notify(规格 §1/FR-11) */
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = cb_battery,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_battery_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ------------------------------------------------------------------ */
/* 广播                                                                */
/* ------------------------------------------------------------------ */

/* 通用可发现广播(配对/回连,规格 §5 输出与 §10 降级链的第二段)。 */
static int adv_general_locked(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)PC_DEVICE_NAME;
    fields.name_len = (uint8_t)strlen(PC_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.appearance = PC_APPEARANCE_GENERIC_HID;
    fields.appearance_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0) s_auto_readvertise = true;
    return rc;
}

/* 定向广播(规格 §10:断连后先定向 30 s;FR-06:槽位切换后对目标槽
 * 定向)。NimBLE 定向广播为高占空比,单轮 ~1.28 s,到期由
 * gap_event 的 ADV_COMPLETE 自动续发(局限 3)。 */
static int adv_directed_locked(const uint8_t addr[6])
{
    /* 定向广播(ADV_DIRECT_IND)不携带广播数据,无需设置字段。
     * NimBLE 定向广播为高占空比,单轮最长 1280 ms。 */
    ble_addr_t peer = { 0 };
    /* 局限 1:槽位元数据未持久化地址类型;有连接记录且地址匹配时
     * 复用其类型,否则按 public 处理(实测若失败,真机侧把槽位
     * 元数据扩展为"地址+类型"即可,属后续里程碑微调)。 */
    peer.type = (s_peer_known && memcmp(s_peer_addr, addr, 6) == 0)
                    ? s_peer_addr_type
                    : BLE_ADDR_PUBLIC;
    memcpy(peer.val, addr, 6);

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_DIR;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    /* 高占空比定向广播最长 1280 ms;到期 ADV_COMPLETE 自动续发。 */
    int rc = ble_gap_adv_start(s_addr_type, &peer, 1280, &params, gap_event, NULL);
    if (rc == 0) s_auto_readvertise = true;
    return rc;
}

/* ------------------------------------------------------------------ */
/* GAP / SM 事件                                                       */
/* ------------------------------------------------------------------ */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                /* 记录对端身份地址(配对成功时组装层取走写槽位) */
                memcpy(s_peer_addr, desc.peer_id_addr.val, 6);
                s_peer_addr_type = desc.peer_id_addr.type;
                s_peer_known = true;
            }
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_host_suspended = false; /* 新连接即视为退出 suspend */
            s_auto_readvertise = false;
            emit(PC_BLE_EVT_CONNECTED, 0);
        } else {
            ESP_LOGW(TAG, "connect failed: %d", event->connect.status);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        emit(PC_BLE_EVT_DISCONNECTED, 0);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* 配对页通用广播自动续发;定向广播到期自动续发直至
         * 组装层显式停止(规格 §10 的 30 s / 2 min 窗口由组装层
         * 的定时器控制时长,本层只负责"续")。 */
        if (s_auto_readvertise && !s_connected) {
            int rc = s_adv_directed ? adv_directed_locked(s_adv_direct_addr)
                                    : adv_general_locked();
            if (rc != 0) {
                ESP_LOGW(TAG, "re-advertise failed: %d", rc);
                s_auto_readvertise = false;
            }
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            /* 6 位配对码(100000..999999,规格 §1/FR-07):
             * 注入 SM 并经回调上报组装层屏显。 */
            uint32_t passkey = 100000 + (esp_random() % 900000);
            struct ble_sm_io pk = { 0 };
            pk.action = BLE_SM_IOACT_DISP;
            pk.passkey = passkey;
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_sm_inject_io failed: %d", rc);
                return 0;
            }
            emit(PC_BLE_EVT_PASSKEY, passkey);
        }
        /* 其它 IO 能力(如 NUMCMP)在 DISPLAY_ONLY 下不会出现;
         * 若主机退化 Just Works(局限 2),本事件根本不触发。 */
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* 加密建立即绑定生效(配对成功的可观察点) */
        emit(event->enc_change.status == 0 ? PC_BLE_EVT_PAIR_OK
                                           : PC_BLE_EVT_PAIR_FAIL,
             0);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* 主机订阅/退订 notify(电量、报告):无需动作,仅日志。 */
        ESP_LOGI(TAG, "subscribe: attr=%d notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    /* host 复位:记录原因;连接标志随之失效,等待重新 sync。 */
    ESP_LOGW(TAG, "nimble host reset, reason=%d", reason);
    s_connected = false;
}

/* 与 demo_ble.c on_sync(行 79-88)同构:取地址 -> 推断类型 ->
 * 若启动前已请求广播则立即发起。 */
static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0 && s_auto_readvertise) rc = adv_general_locked();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble on_sync failed: %d", rc);
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run(); /* 返回即 host 已停止 */
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

/* ------------------------------------------------------------------ */
/* 公共接口                                                            */
/* ------------------------------------------------------------------ */

esp_err_t pc_ble_hid_init(void (*cb)(pc_ble_hid_evt_t, uint32_t arg, void *user), void *user)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    if (s_initialized) return ESP_ERR_INVALID_STATE;

    s_evt_cb = cb;
    s_evt_user = user;

    /* 1. NVS 前置(失败不擦除;NimBLE 绑定记录存其默认命名空间,
     *    由 CONFIG_BT_NIMBLE_NVS_PERSIST=y 开启,规格 §2/§8)。 */
    esp_err_t err = nvs_prepare_no_erase();
    if (err != ESP_OK) return err;

    /* 2. Report Map 从纯逻辑层取到静态缓冲 */
    s_report_map_len = pc_hid_report_map(s_report_map, (int)sizeof(s_report_map));
    if (s_report_map_len <= 0 || s_report_map_len > (int)sizeof(s_report_map)) {
        ESP_LOGE(TAG, "report map too large: %d", s_report_map_len);
        return ESP_ERR_NO_MEM;
    }

    /* 3. host 栈初始化(与 demo_ble.c ble_start 同序) */
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_initialized = true;

    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        nimble_port_deinit();
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    /* 4. GAP/GATT 服务与设备名 "PC-CTRL"、Appearance 0x03C1 */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(PC_DEVICE_NAME);
    if (rc != 0) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        nimble_port_deinit();
        s_initialized = false;
        return ESP_FAIL;
    }
    ble_svc_gap_device_appearance_set(PC_APPEARANCE_GENERIC_HID);

    /* 5. 注册 HOGP 服务表 */
    rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        nimble_port_deinit();
        s_initialized = false;
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
        nimble_port_deinit();
        s_initialized = false;
        return ESP_FAIL;
    }

    /* 6. host 回调与配对安全配置(规格 §1/FR-07、§13):
     *    DISPLAY_ONLY + SC + MITM + bonding。
     *    已知风险:部分主机仍会退化 Just Works(规格 §13),此时
     *    不触发 passkey 事件,配对照旧完成。 */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_bonding = 1;

    /* 7. 拉起 host 任务 */
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "HOGP stack started, name=%s", PC_DEVICE_NAME);
    return ESP_OK;
}

esp_err_t pc_ble_hid_start_adv_general(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_connected) return ESP_ERR_INVALID_STATE;
    ble_gap_adv_stop();
    s_adv_directed = false;
    int rc = adv_general_locked();
    if (rc != 0) {
        s_auto_readvertise = false;
        ESP_LOGE(TAG, "general adv failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t pc_ble_hid_start_adv_directed(const uint8_t addr[6])
{
    if (!s_initialized || addr == NULL) return ESP_ERR_INVALID_ARG;
    if (s_connected) return ESP_ERR_INVALID_STATE;
    ble_gap_adv_stop();
    /* 定向广播单轮 ~1.28 s,到期 ADV_COMPLETE 后按同一目标地址续发;
     * 窗口时长(30 s / 2 min)由组装层定时器到时后调 stop_adv。 */
    memcpy(s_adv_direct_addr, addr, 6);
    s_adv_directed = true;
    int rc = adv_directed_locked(addr);
    if (rc != 0) {
        ESP_LOGE(TAG, "directed adv failed: %d", rc);
        return ESP_FAIL;
    }
    s_auto_readvertise = true;
    return ESP_OK;
}

esp_err_t pc_ble_hid_stop_adv(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_auto_readvertise = false;
    s_adv_directed = false;
    ble_gap_adv_stop();
    return ESP_OK;
}

bool pc_ble_hid_connected(void)
{
    return s_connected;
}

/* 底层发送:固定负载 + Report ID 前缀 -> mbuf -> notify。
 * mbuf 所有权移交给 ble_gatts_notify_custom(成功失败均被消费)。 */
static esp_err_t report_notify(uint16_t handle, const uint8_t *payload, int len)
{
    if (!s_initialized || !s_connected) return ESP_ERR_INVALID_STATE;
    if (s_host_suspended) return ESP_ERR_INVALID_STATE; /* HOGP suspend */

    uint8_t buf[16];
    if (len + 1 > (int)sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    /* Report ID 前缀由平台层按特征/引用附加(见
     * pc_hid_reports.h 与 Report Reference 的分工注释)。 */
    buf[0] = (handle == s_kbd_report_handle) ? PC_REPORT_ID_KEYBOARD : PC_REPORT_ID_CONSUMER;
    memcpy(buf + 1, payload, len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len + 1);
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t pc_ble_hid_send_keyboard(const pc_kbd_report_t *r)
{
    if (r == NULL) return ESP_ERR_INVALID_ARG;
    /* pc_kbd_report_t = mods + reserved + keys[6],共 8 字节连续 */
    memcpy(s_last_kbd, r, sizeof(s_last_kbd));
    esp_err_t err = report_notify(s_kbd_report_handle, (const uint8_t *)r, sizeof(*r));
    if (err != ESP_OK) return err;

    /* 必跟空报告释放按键(规格 §1/FR-01:无卡键) */
    pc_kbd_report_t empty;
    pc_kbd_clear(&empty);
    memcpy(s_last_kbd, &empty, sizeof(s_last_kbd));
    return report_notify(s_kbd_report_handle, (const uint8_t *)&empty, sizeof(empty));
}

esp_err_t pc_ble_hid_send_consumer(uint16_t usage)
{
    uint8_t press[2];
    pc_consumer_pack(usage, press);
    memcpy(s_last_consumer, press, sizeof(press));
    esp_err_t err = report_notify(s_consumer_report_handle, press, sizeof(press));
    if (err != ESP_OK) return err;

    /* release 帧:usage 0(规格 §5:press + release 两帧) */
    uint8_t release[2];
    pc_consumer_pack(0, release);
    memcpy(s_last_consumer, release, sizeof(release));
    return report_notify(s_consumer_report_handle, release, sizeof(release));
}

esp_err_t pc_ble_hid_battery_notify(uint8_t percent)
{
    s_battery_percent = percent;
    if (!s_initialized || !s_connected) return ESP_ERR_INVALID_STATE;
    /* notify 不带 Report ID 前缀(非 HID Report 特征) */
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&s_battery_percent, 1);
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_battery_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t pc_ble_hid_graceful_disconnect(void)
{
    if (!s_initialized || !s_connected) return ESP_ERR_INVALID_STATE;
    /* FR-06:优雅断开;断开后设备按 HOGP 语义进 suspend 态——
     * 表现为:停止一切 HID 发送、不再主动通知,等待主机回连
     * (定向广播)或主机下 Suspend 指令。 */
    int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERMINATED);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        ESP_LOGE(TAG, "graceful disconnect failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t pc_ble_hid_peer_addr(uint8_t out[6])
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_peer_known) return ESP_ERR_INVALID_STATE;
    memcpy(out, s_peer_addr, 6);
    return ESP_OK;
}

esp_err_t pc_ble_hid_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_auto_readvertise = false;
    if (s_connected) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERMINATED);
    }
    ble_gap_adv_stop();

    int rc = nimble_port_stop();
    if (rc == 0 && s_host_stopped) {
        /* host 回调不访问 LVGL;持有其它锁也不会形成锁环
         * (与 demo_ble.c ble_stop 的同款说明)。 */
        xSemaphoreTake(s_host_stopped, portMAX_DELAY);
    }
    if (rc == 0) {
        nimble_port_deinit();
        s_initialized = false;
        s_connected = false;
    } else {
        ESP_LOGE(TAG, "nimble_port_stop failed: %d", rc);
    }
    if (!s_initialized && s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
