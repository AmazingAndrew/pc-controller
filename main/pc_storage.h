// main/pc_storage.h
// PC Controller 平台层:配置与槽位元数据的 NVS 持久化(规格 §8)。
//
// 分区布局(规格 §8,24 KB NVS 分区,不触碰分区表本身):
//   命名空间 `pp_cfg`      全局配置(背光、按键音、当前槽、默认主机配置)
//   命名空间 `pp_slot0..2` 每槽一份元数据(绑定地址、主机名、OS 类型、
//                          锁屏组合键、最近使用时间)
//   NimBLE 默认命名空间     绑定记录,由 CONFIG_BT_NIMBLE_NVS_PERSIST=y
//                          自动维护(见 sdkconfig.presenter.defaults)
//
// 失败降级(规格 §10):
//   - 初始化沿用 demo_radio_nvs_prepare() 的"失败不擦除"模式——
//     绝不为了启动功能而擦分区;
//   - 任何读取失败(未初始化/键缺失/校验错)一律回填内存默认值并
//     记日志,调用方永远拿到可用数据。
//
// 线程上下文:全部接口设计为应用任务调用(启动阶段 + 事件循环),
// 非阻塞;NVS 写盘耗时(典型 < 10 ms)由调用方接受。
// 内存所有权:只写调用方提供的结构体,内部分配仅由 NVS 句柄临时持有。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* 槽位数量:固定 3(规格 §1:三设备槽位,串行切换)。 */
#define PC_SLOT_COUNT 3

/* 全局配置(命名空间 `pp_cfg`)。
 * 键与类型:
 *   "backlight"  u8  背光百分比 0..100,默认 100
 *   "key_sound"  u8  按键音开关 0/1,默认 0(规格 §1/FR-09:默认关)
 *   "slot"       u8  当前槽位索引 0..2,默认 0
 *   "default_os" u8  默认主机配置(新配对槽的 OS 类型,
 *                    取值见 pc_host_profiles.h 的 pc_os_t),默认 0
 *                    (Windows) */
typedef struct {
    uint8_t backlight;  /* 背光百分比 0..100(电源任务后续接入) */
    bool key_sound;     /* 按键音开关(媒体/蜂鸣任务后续接入) */
    uint8_t slot;       /* 当前槽位索引 0..2 */
    uint8_t default_os; /* 新配对槽位记录的默认 OS 类型(pc_os_t) */
} pc_cfg_t;

/* 单槽元数据(命名空间 `pp_slot0`..`pp_slot2`)。
 * 键与类型:
 *   "addr"      blob 6 字节对端身份地址;键存在即视为已绑定
 *   "host_name" str  主机显示名(<= 16 字符 + '\0'),未绑定为空串
 *   "os"        u8   主机 OS 类型(pc_os_t)
 *   "lock_mods" u8   锁屏组合键修饰位(0 = 未记录,锁屏时按 os 回退
 *                    到 pc_host_profiles 表)
 *   "lock_key"  u8   锁屏组合键键码(同上)
 *   "last_use"  u32  最近使用时间戳(组装层以运行期秒数写入) */
typedef struct {
    uint8_t addr[6];      /* 对端身份地址(配对成功时写入) */
    bool bound;           /* 是否已绑定(= "addr" 键存在且非全零) */
    char host_name[17];   /* 主机显示名,恒以 '\0' 结尾 */
    uint8_t os;           /* 主机 OS 类型,取值 0..PC_OS_MAX-1 */
    uint8_t lock_mods;    /* 锁屏组合键修饰位(0 = 未记录) */
    uint8_t lock_key;     /* 锁屏组合键键码 */
    uint32_t last_use;    /* 最近使用时间戳(组装层语义) */
} pc_slot_t;

/* 初始化 NVS(幂等)。
 * 行为:沿用 demo_radio_nvs_prepare()(demo_radio.c 行 14-26)的
 *      "失败不擦除"模式——nvs_flash_init 失败仅记日志返回错误码,
 *      绝不擦分区;后续所有读取自动降级为内存默认值(规格 §10)。
 *      注意:presenter 档不编译 demo_radio.c,故该模式在本模块内
 *      复刻,与 pc_ble_hid 内的同款前置互不影响(幂等)。
 * 返回值:ESP_OK;底层失败透传(组装层记日志后继续运行)。
 * 线程上下文:启动阶段单线程调用。 */
esp_err_t pc_storage_init(void);

/* 载入全局配置。
 * 失败降级:任一失败(未初始化/键缺失)回填默认值(背光 100、
 *           按键音关、槽 0、默认 OS = Windows)并记日志,仍返回
 *           ESP_OK 以外的错误码供调用方记录;结构体保证可用。
 * 参数:cfg 输出,不可为 NULL。
 * 线程上下文:应用任务;非阻塞。 */
esp_err_t pc_cfg_load(pc_cfg_t *cfg);

/* 保存全局配置。
 * 返回值:ESP_OK;未初始化或写盘失败返回错误码(组装层屏显降级,
 *        功能不受影响——配置只是下次启动的默认值)。
 * 参数:cfg 不可为 NULL。 */
esp_err_t pc_cfg_save(const pc_cfg_t *cfg);

/* 载入第 i 槽元数据。
 * 参数:
 *   i 槽位索引 0..2;越界返回 ESP_ERR_INVALID_ARG 且不写 *s。
 *   s 输出,不可为 NULL。
 * 失败降级:未绑定/未初始化/读失败时回填"未绑定空槽"默认值
 *           (全零地址、bound = false、空主机名、os = 0)并记日志,
 *           返回错误码;结构体保证可用。 */
esp_err_t pc_slot_load(uint8_t i, pc_slot_t *s);

/* 保存第 i 槽元数据(配对成功/槽位信息更新时调用)。
 * 返回值:同 pc_cfg_save;参数越界返回 ESP_ERR_INVALID_ARG。 */
esp_err_t pc_slot_save(uint8_t i, const pc_slot_t *s);

/* 清除第 i 槽绑定(菜单 CLEAR SLOT,规格 §1/FR-08)。
 * 行为:仅删除该槽命名空间内的全部键(等价于恢复"未绑定空槽"),
 *       绝不擦分区。注意:对应主机的 NimBLE 绑定记录不在本接口
 *       清除范围内——FR-08 的流程是"主机侧先忘记设备,再重新配对
 *       覆盖旧绑定"(用户指南要求写明),本模块不做跨命名空间删除。
 * 返回值:ESP_OK;越界返回 ESP_ERR_INVALID_ARG。 */
esp_err_t pc_slot_clear(uint8_t i);

/* 是否存在任一已绑定槽位(供首启自动配对判定,规格 §1/FR-07:
 * 无绑定上电直接进配对模式)。
 * 返回值:任一槽的 "addr" 键存在且非全零即返回 true。
 * 线程上下文:启动阶段调用;内部按槽顺序只读查询。 */
bool pc_any_slot_bound(void);
