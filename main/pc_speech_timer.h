// main/pc_speech_timer.h
// 演讲计时器(平台无关纯逻辑模块)。
//
// 职责:以秒为单位记录演讲已用时长(规格 §1/FR-05):
//   - 每次进入演示模式清零(由组装层在收到状态机的
//     PC_FX_TIMER_RESET effect 时调用);
//   - 由 1 Hz 定时源驱动走时(规格 §7:esp_timer 1 Hz tick,
//     平台层把 tick 映射到 pc_speech_tick);
//   - 支持暂停/恢复(任务 #47):OK 长按切换, 暂停期间不
//     自增, 状态由 paused 字段跟踪。
//
// 走时精度说明:本模块只负责"每调用一次 +1 秒"的纯计数,
// 漂移取决于平台 1 Hz 定时源的精度;FR-05 验收指标
// (30 分钟漂移 <= 数秒)由平台层定时器保证。
//
// 平台无关性:仅依赖 C11 标准库,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 演讲计时器实例。
 * 生命周期:由组装层持有(应用任务内),进入演示模式时清零。 */
typedef struct {
    /* 已用秒数。取值 0 .. 0xFFFFFFFF;按 1 Hz 走时约可计到
     * 136 年,实际不存在溢出场景。 */
    uint32_t sec;
    /* 暂停标记 (任务 #47): 暂停期间 pc_speech_tick() 不自增;
     * 由组装层在收到 PC_ACT_TIMER_TOGGLE 时翻转, 与 UI 的
     * "PAUSED" 指示同步。 */
    bool paused;
} pc_speech_timer_t;

/* 计时器清零。
 * 调用时机:每次进入演示模式(规格 §1/FR-05:进入即显示 00:00)。
 * 参数:
 *   t 计时器实例,不可为 NULL(为 NULL 时直接返回)。
 * 线程上下文:应用任务;无阻塞,无分配。
 * 内存所有权:写调用方对象。 */
void pc_speech_reset(pc_speech_timer_t *t);

/* 计时器走一拍(+1 秒)。由 1 Hz 定时源驱动;仅在演示模式
 * 活跃期间由组装层调用,其它模式不走时。
 * 参数:
 *   t 计时器实例,不可为 NULL(为 NULL 时直接返回)。
 * 副作用:sec 自增;达到 0xFFFFFFFF 后自然回绕(理论 136 年,
 *        实际不可达,不做饱和处理)。
 * 任务 #47:暂停期间 (t->paused == true) 调用此函数为无效操作,
 *        sec 保持不变,不触发任何 IO。组装层须保证 1 Hz tick 的
 *        调用方在状态机进入 PRESENT 后开启, 退出后停止。
 * 线程上下文:应用任务(定时器回调统一归一到应用任务后调用)。 */
void pc_speech_tick(pc_speech_timer_t *t);

/* 读取已用秒数。
 * 返回值:已用秒数;实例为 NULL 时返回 0(防御式降级)。
 * 线程上下文:任意;无阻塞。 */
uint32_t pc_speech_seconds(const pc_speech_timer_t *t);

/* 读取暂停状态。
 * 返回值:true = 已暂停; false = 走时中; 实例为 NULL 时返回 false。
 * 线程上下文:任意;无阻塞。 */
bool pc_speech_is_paused(const pc_speech_timer_t *t);

/* 设置暂停状态 (任务 #47)。
 * 参数:
 *   t      计时器实例,不可为 NULL (为 NULL 时直接返回)。
 *   paused true = 进入暂停, 后续 tick 不再自增;
 *          false = 恢复走时, 后续 tick 恢复自增。
 * 调用时机: 由组装层在收到 PC_ACT_TIMER_TOGGLE 时翻转;
 *       该函数为幂等 (多次同值调用安全)。
 * 线程上下文:应用任务; 无阻塞, 无分配。 */
void pc_speech_set_paused(pc_speech_timer_t *t, bool paused);

/* 把已用时长格式化为屏显字符串。
 * 格式自适应 (任务 #47):
 *   - < 1 小时 (sec < 3600): "MM:SS"
 *   - >= 1 小时 (sec >= 3600): "HH:MM:SS"
 *   其中分钟位/小时位均左侧补零。
 * 上限钳制: 超出 99:59:59 的时长按 99:59:59 显示 (8 字符 + '\0'
 *       = 9 字节), 演讲时长超 100 小时无显示意义, 钳制是成本最
 *       低的防御 (避免截断出残缺字符串)。
 * 参数:
 *   t   计时器实例; 为 NULL 时输出 "00:00" (防御式降级)。
 *   out 9 字节输出缓冲, 不可为 NULL (为 NULL 时直接返回)。
 *       保证以 '\0' 结尾且总写入不超过 9 字节。
 * 线程上下文:任意; 无阻塞, 无分配。 内存所有权: 写调用方缓冲。 */
void pc_speech_format(const pc_speech_timer_t *t, char out[9]);
