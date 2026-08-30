// main/pc_slide_counter.h
// 页码来源抽象(平台无关纯逻辑模块,规格 §11 扩展性)。
//
// 背景:页码显示走"来源接口",默认实现是本机估算器
// (进入全屏从 1 开始计数,翻页 ±1,规格 §1/FR-12)。未来可以通过
// 伴机 (companion) 侧 GATT 拿到真实页码——届时新增一个
// `companion_page_source` 实现同一接口即可,UI 与组装层零改动
// (规格 §11 第 1 条)。
//
// 接口模型:极简"api 指针"多态——基类结构体只含一个指向虚表
// (pc_page_source_api_t) 的指针;具体实现可扩展该结构体(首成员必须
// 是 pc_page_source_t)来携带私有状态。
//
// 平台无关性:仅依赖 C11 标准库,可被
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
// 直接编译,供 host 测试使用。
#pragma once

#include <stdint.h>

/* 页码来源基类(前向声明,见下方定义)。 */
typedef struct pc_page_source pc_page_source_t;

/* 页码来源虚表。所有实现必须提供的唯一操作:读取当前页码。 */
typedef struct {
    /* 读取当前页码。
     * 参数:s 来源实例(实现可忽略该指针直接读自身静态状态)。
     * 返回值:当前页码(>= 1);未进入全屏/不可用时返回 -1。
     * 线程上下文:应用任务;无阻塞。 */
    int (*get_page)(const pc_page_source_t *s);
} pc_page_source_api_t;

/* 页码来源基类结构体。具体实现以它为结构体首成员
 * (C 语言的"继承"惯例),从而可安全上/下转型。 */
struct pc_page_source {
    const pc_page_source_api_t *api; /* 虚表指针,初始化后不变 */
};

/* 通过接口读取当前页码(多态分发)。
 * 返回值:当前页码(>= 1);来源为 NULL、api 缺失、或未进入
 *        全屏时返回 -1,屏显层收到 -1 应不画页码(优雅降级)。
 * 线程上下文:应用任务;无阻塞,无分配。 */
int pc_page_get(const pc_page_source_t *s);

/* ---- 默认实现:本机估算器(规格 §1/FR-12) ---- */

/* 取本机估算器的静态单例。
 * 返回值:进程内唯一的页码来源实例;生命周期为整个运行期,
 *        调用方不得释放。多次调用返回同一指针。
 * 初始状态:未进全屏,页码读数为 -1。
 * 线程上下文:任意;首次调用内部完成一次性初始化(静态存储,
 * 无并发竞争窗口需要调用方处理——组装层在启动期调用)。 */
pc_page_source_t *pc_local_page_source(void);

/* 通知本机估算器"已进入全屏":页码复位为 1(规格 §1/FR-12)。
 * 参数:
 *   s 必须是 pc_local_page_source() 返回的实例;其它实现的实例
 *     传入时被忽略(防御式)。
 * 线程上下文:应用任务;无阻塞。 */
void pc_local_fullscreen_entered(pc_page_source_t *s);

/* 本机估算器页码步进(翻页后调用)。
 * 参数:
 *   s     同上。
 *   delta 步进量,典型 ±1;允许多页跳转(例如长按连翻)。
 * 行为:仅在"已进入全屏"时生效;结果钳制到 >= 1
 *      (页数不可能小于 1,规格 §1/FR-12)。
 * 线程上下文:应用任务;无阻塞。 */
void pc_local_step(pc_page_source_t *s, int delta);

/* 通知本机估算器"已退出全屏":停止计数,页码读数回到 -1。
 * 参数:同 pc_local_fullscreen_entered。
 * 线程上下文:应用任务;无阻塞。 */
void pc_local_fullscreen_exited(pc_page_source_t *s);
