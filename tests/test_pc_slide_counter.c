// tests/test_pc_slide_counter.c
// 页码来源(本机估算器)host 测试(纯 C11 assert,无框架,范式见
// tests/test_ui_pixel_math.c)。
//
// 事实源:规格 §1/FR-12——页码为本地估算:进入全屏从 1 开始计数,
// 翻页 ±1,钳制到 >= 1;退出全屏停止计数;无总页数、无 "EST" 标记。
// 注意:本机估算器是静态单例,本测试按状态机生命周期顺序推进,
// 各断言之间存在先后依赖(与真实使用顺序一致)。
// 编译命令:
//   gcc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_pc_slide_counter.c main/pc_slide_counter.c
#include <assert.h>
#include <stddef.h>

#include "pc_slide_counter.h"

int main(void)
{
    pc_page_source_t *s = pc_local_page_source();

    /* 单例:多次调用返回同一实例。 */
    assert(s == pc_local_page_source());
    assert(s != NULL);
    assert(s->api != NULL);

    /* 未进入全屏:读数为 -1(屏显不画页码)。 */
    assert(pc_page_get(s) == -1);

    /* 未进入全屏时翻页不计数(页码语义只存在于全屏场景)。 */
    pc_local_step(s, 1);
    assert(pc_page_get(s) == -1);

    /* 进入全屏:页码复位为 1。 */
    pc_local_fullscreen_entered(s);
    assert(pc_page_get(s) == 1);

    /* 翻到下一页:+1 -> 2。 */
    pc_local_step(s, 1);
    assert(pc_page_get(s) == 2);

    /* 翻回上一页:-1 -> 1。 */
    pc_local_step(s, -1);
    assert(pc_page_get(s) == 1);

    /* 在第 1 页再翻上一页:钳制到 >= 1(页数不可能小于 1)。 */
    pc_local_step(s, -1);
    assert(pc_page_get(s) == 1);
    pc_local_step(s, -5); /* 大幅回退同样钳制 */
    assert(pc_page_get(s) == 1);

    /* 允许多页跳转(如长按连翻)。 */
    pc_local_step(s, 9);
    assert(pc_page_get(s) == 10);

    /* 退出全屏:停止计数,读数回到 -1。 */
    pc_local_fullscreen_exited(s);
    assert(pc_page_get(s) == -1);

    /* 退出后再翻页无效。 */
    pc_local_step(s, 3);
    assert(pc_page_get(s) == -1);

    /* 再次进入全屏:从 1 重新开始(规格 §1/FR-12 验收:
     * "after re-entering full screen it restarts from 1")。 */
    pc_local_fullscreen_entered(s);
    assert(pc_page_get(s) == 1);
    pc_local_fullscreen_exited(s);

    /* ======== vtable / 接口分发路径 ======== */

    /* 来源为 NULL:优雅降级 -1。 */
    assert(pc_page_get(NULL) == -1);

    /* api 缺失:防御式 -1。 */
    {
        pc_page_source_t bare = { NULL };
        assert(pc_page_get(&bare) == -1);
    }

    /* 非本机实现的实例误传给本地接口:被忽略,不产生副作用。
     * (先确认本地单例处于已知状态:未全屏 -> -1。) */
    assert(pc_page_get(s) == -1);
    {
        pc_page_source_t foreign = { NULL };
        pc_local_fullscreen_entered(&foreign);
        pc_local_step(&foreign, 4);
        pc_local_fullscreen_exited(&foreign);
    }
    assert(pc_page_get(s) == -1); /* 本地单例状态未被污染 */

    return 0;
}
