// main/pc_storage.c —— PC Controller 平台层:配置与槽位元数据持久化。
//
// 事实源:规格 §8 持久化布局、§10 失败降级、§1/FR-07(首启自动
// 配对判定)、§1/FR-08(清槽只删命名空间键、不擦分区)。
//
// 设计要点:
//   - 全部读接口"失败即默认值 + 日志"(规格 §10);调用方永远拿到
//     可用结构体,无需逐点判错;
//   - 写接口返回错误码,由组装层决定屏显反馈(配置丢失只影响
//     下次启动的默认值,不阻塞功能);
//   - 命名空间命名按规格 §8:`pp_cfg` / `pp_slot0`..`pp_slot2`。
#include "pc_storage.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "pc_ble_hid.h" /* pc_ble_hid_clear_bond (#55:清槽同时删 NimBLE 绑定) */

#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_storage";

/* 命名空间名(规格 §8)。 */
#define NS_CFG "pp_cfg"
#define NS_SLOT_PREFIX "pp_slot" /* + '0'..'2' */

/* 配置默认值(键缺失/读失败时回填)。 */
#define CFG_DEF_BACKLIGHT 100 /* 全亮,规格 §1 背光策略的活跃档 */
#define CFG_DEF_KEY_SOUND false /* 规格 §1/FR-09:按键音默认关 */
#define CFG_DEF_SLOT 0
#define CFG_DEF_DEFAULT_OS 0 /* PC_OS_WINDOWS(pc_host_profiles.h) */

static bool s_nvs_ready;

/* "addr" 键存在且非全零 => 已绑定。全零广播地址非法,故全零
 * 记录视为"未绑定"(防御旧版本/手工写入的脏数据)。 */
static bool addr_nonzero(const uint8_t addr[6])
{
    for (int i = 0; i < 6; i++) {
        if (addr[i] != 0) return true;
    }
    return false;
}

/* 拼槽位命名空间名:"pp_slot0".."pp_slot2"。
 * 前置:调用方已保证 i < PC_SLOT_COUNT。
 * 缓冲区 16 字节:NVS 命名空间名上限 15 字符 + '\0'。
 * 留宽以避免 GCC -Wformat-truncation 误报(`uint8_t` 形参可
 * 视为 unsigned int,最坏输出 3 位数字 + 7 字节前缀 = 11 字节)。 */
static void slot_ns_name(uint8_t i, char out[16])
{
    snprintf(out, 16, "%s%u", NS_SLOT_PREFIX, (unsigned)i);
}

esp_err_t pc_storage_init(void)
{
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* 与 demo_radio_nvs_prepare()(demo_radio.c 行 14-26)同模式:
         * 初始化失败仅记日志,绝不擦分区——分区里可能有同机其它
         * 应用/命名空间的数据;后续读取全部降级为内存默认值
         * (规格 §10)。 */
        ESP_LOGE(TAG, "NVS init failed: %s; partition NOT erased, "
                      "falling back to in-memory defaults",
                 esp_err_to_name(err));
        return err;
    }
    s_nvs_ready = true;
    return ESP_OK;
}

/* ---- 内存默认值回填 ---- */

static void cfg_defaults(pc_cfg_t *cfg)
{
    cfg->backlight = CFG_DEF_BACKLIGHT;
    cfg->key_sound = CFG_DEF_KEY_SOUND;
    cfg->slot = CFG_DEF_SLOT;
    cfg->default_os = CFG_DEF_DEFAULT_OS;
}

static void slot_defaults(pc_slot_t *s)
{
    memset(s, 0, sizeof(*s)); /* addr 全零、addr_type = 0 (= BLE_ADDR_PUBLIC,
                                * 默认 public 向下兼容)、os = 0、组合键 0、
                                * 时间戳 0 */
    s->bound = false;
    s->addr_type = 0; /* BLE_ADDR_PUBLIC(防御:NVS 错误清零的边界) */
    /* host_name 已被 memset 清为 '\0' */
}

esp_err_t pc_cfg_load(pc_cfg_t *cfg)
{
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    cfg_defaults(cfg);
    if (!s_nvs_ready) {
        ESP_LOGW(TAG, "cfg load: NVS not ready, using defaults");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* 首次启动无该命名空间属正常路径,降级为默认值 */
        ESP_LOGI(TAG, "cfg load: open failed (%s), using defaults", esp_err_to_name(err));
        return err;
    }

    uint8_t u8;
    if (nvs_get_u8(h, "backlight", &u8) == ESP_OK) cfg->backlight = u8;
    if (nvs_get_u8(h, "key_sound", &u8) == ESP_OK) cfg->key_sound = (u8 != 0);
    if (nvs_get_u8(h, "slot", &u8) == ESP_OK) {
        cfg->slot = (u8 < PC_SLOT_COUNT) ? u8 : CFG_DEF_SLOT; /* 防御越界 */
    }
    if (nvs_get_u8(h, "default_os", &u8) == ESP_OK) cfg->default_os = u8;

    nvs_close(h);
    return ESP_OK;
}

esp_err_t pc_cfg_save(const pc_cfg_t *cfg)
{
    if (cfg == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_nvs_ready) {
        ESP_LOGW(TAG, "cfg save: NVS not ready, skip");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_CFG, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cfg save: open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(h, "backlight", cfg->backlight);
    if (err == ESP_OK) err = nvs_set_u8(h, "key_sound", cfg->key_sound ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(h, "slot", cfg->slot);
    if (err == ESP_OK) err = nvs_set_u8(h, "default_os", cfg->default_os);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "cfg save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t pc_slot_load(uint8_t i, pc_slot_t *s)
{
    if (s == NULL || i >= PC_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    slot_defaults(s);
    if (!s_nvs_ready) {
        ESP_LOGW(TAG, "slot %u load: NVS not ready, using defaults", (unsigned)i);
        return ESP_ERR_INVALID_STATE;
    }

    char ns[16];
    slot_ns_name(i, ns);
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "slot %u load: open failed (%s), treating as unbound",
                 (unsigned)i, esp_err_to_name(err));
        return err;
    }

    /* addr:6 字节 blob,键缺失即未绑定 */
    uint8_t addr[6];
    size_t len = sizeof(addr);
    if (nvs_get_blob(h, "addr", addr, &len) == ESP_OK && len == sizeof(addr) &&
        addr_nonzero(addr)) {
        memcpy(s->addr, addr, sizeof(addr));
        s->bound = true;
    }

    /* addr_type:u8(#54)——键缺失默认 BLE_ADDR_PUBLIC(=0),与旧版
     * (未持久化 addr_type)创建的槽位向下兼容。 */
    uint8_t at = 0;
    if (nvs_get_u8(h, "addr_type", &at) == ESP_OK) {
        s->addr_type = at;
    } else {
        s->addr_type = 0; /* BLE_ADDR_PUBLIC */
    }

    /* host_name:字符串,容量 17 字节(含 '\0') */
    size_t name_len = sizeof(s->host_name);
    if (nvs_get_str(h, "host_name", s->host_name, &name_len) != ESP_OK) {
        s->host_name[0] = '\0';
    }

    uint8_t u8;
    if (nvs_get_u8(h, "os", &u8) == ESP_OK) s->os = u8;
    if (nvs_get_u8(h, "lock_mods", &u8) == ESP_OK) s->lock_mods = u8;
    if (nvs_get_u8(h, "lock_key", &u8) == ESP_OK) s->lock_key = u8;
    nvs_get_u32(h, "last_use", &s->last_use); /* 可缺失,缺省 0 */

    nvs_close(h);
    return ESP_OK;
}

esp_err_t pc_slot_save(uint8_t i, const pc_slot_t *s)
{
    if (s == NULL || i >= PC_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_nvs_ready) {
        ESP_LOGW(TAG, "slot %u save: NVS not ready, skip", (unsigned)i);
        return ESP_ERR_INVALID_STATE;
    }

    char ns[16];
    slot_ns_name(i, ns);
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "slot %u save: open failed: %s", (unsigned)i, esp_err_to_name(err));
        return err;
    }

    if (s->bound) {
        err = nvs_set_blob(h, "addr", s->addr, sizeof(s->addr));
        if (err == ESP_OK) {
            /* #54:同步写 addr_type;绑定存在时必写——即使与上次一致
             * 也写一次,以便旧槽(只写了 addr)在本次保存后具备完整字段。 */
            err = nvs_set_u8(h, "addr_type", s->addr_type);
        }
    } else {
        /* 未绑定槽不留 "addr" 键,保持"键存在 = 已绑定"语义。
         * 注:ESP-IDF v5.x 的 NVS API 已将 nvs_delete_key 重命名为
         * nvs_erase_key(原符号在新版本 NVS 头文件中已移除,
         * 编译报 implicit-function-declaration)。 */
        nvs_erase_key(h, "addr");
        /* addr_type 同样不写(只对已绑定槽有效)。 */
        nvs_erase_key(h, "addr_type");
    }
    if (err == ESP_OK) err = nvs_set_str(h, "host_name", s->host_name);
    if (err == ESP_OK) err = nvs_set_u8(h, "os", s->os);
    if (err == ESP_OK) err = nvs_set_u8(h, "lock_mods", s->lock_mods);
    if (err == ESP_OK) err = nvs_set_u8(h, "lock_key", s->lock_key);
    if (err == ESP_OK) err = nvs_set_u32(h, "last_use", s->last_use);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "slot %u save failed: %s", (unsigned)i, esp_err_to_name(err));
    }
    return err;
}

esp_err_t pc_slot_clear(uint8_t i)
{
    if (i >= PC_SLOT_COUNT) return ESP_ERR_INVALID_ARG;
    if (!s_nvs_ready) {
        ESP_LOGW(TAG, "slot %u clear: NVS not ready, skip", (unsigned)i);
        return ESP_ERR_INVALID_STATE;
    }

    /* #55:先读槽位拿 addr + addr_type,然后删 NimBLE 绑定记录。
     * NimBLE 的绑定存储在 ble_hs / ble_store 自己的 flash 区,不会随
     * 本模块命名空间的 nvs_erase_all 被擦除——必须显式调
     * ble_gap_unpair 删除,否则主机侧长期 key 仍保留,会导致下次同
     * 地址配对时旧 LTK 被重复使用、或主机自动重连一个已被用户
     * "清除"的槽位。删除失败仅记 warning,继续走 NVS 擦除——
     * 清除槽位的用户意图不能被绑定删除失败拖住。 */
    pc_slot_t s;
    (void)pc_slot_load(i, &s); /* 失败已回填默认值 */
    if (s.bound) {
        esp_err_t br = pc_ble_hid_clear_bond(s.addr, s.addr_type);
        if (br != ESP_OK) {
            ESP_LOGW(TAG,
                     "slot %u clear: NimBLE bond delete failed (rc=%d); "
                     "NVS metadata erased anyway",
                     (unsigned)i, br);
        } else {
            ESP_LOGI(TAG, "slot %u clear: NimBLE bond deleted", (unsigned)i);
        }
    }

    char ns[16];
    slot_ns_name(i, ns);
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        /* 命名空间不存在 = 本就未绑定,清除等价成功 */
        ESP_LOGI(TAG, "slot %u clear: nothing to clear (%s)", (unsigned)i,
                 esp_err_to_name(err));
        return ESP_OK;
    }
    /* FR-08:只删该槽命名空间内的键,不擦分区。
     * 注意:对应主机的 NimBLE 绑定记录保留,由"主机侧忘记设备后
     * 重新配对覆盖"流程处理(见头文件注释与规格 §1/FR-08)。
     * —— 本里程碑(#55)起该绑定由上方 pc_ble_hid_clear_bond() 显式
     * 删除;此处只清本模块命名空间。 */
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "slot %u clear failed: %s", (unsigned)i, esp_err_to_name(err));
    }
    return err;
}

bool pc_any_slot_bound(void)
{
    if (!s_nvs_ready) return false; /* 读不到 = 视为无绑定 -> 走配对 */

    for (uint8_t i = 0; i < PC_SLOT_COUNT; i++) {
        pc_slot_t s;
        if (pc_slot_load(i, &s) == ESP_OK && s.bound) return true;
    }
    return false;
}
