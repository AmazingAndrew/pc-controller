// main/pc_slide_counter.c
// 页码来源接口分发 + 本机估算器实现。
//
// 本机估算器语义(规格 §1/FR-12):
//   - 未进入全屏:读数为 -1(屏显不画页码);
//   - 进入全屏:计数从 1 开始;
//   - 翻页:+/-1 步进,钳制到 >= 1;
//   - 退出全屏:回到 -1;
//   - 不显示总页数、不显示 "EST" 标记(规格 §1 非目标)。
//
// 平台无关:仅依赖 C11 标准库,可被 host 测试直接编译。
#include "pc_slide_counter.h"

#include <stdbool.h>
#include <stddef.h>

/* 本机估算器实例:基类在首成员位置(可安全上转型为
 * pc_page_source_t *),其后携带私有状态。 */
typedef struct {
    pc_page_source_t base; /* 必须是首成员,见头文件接口模型说明 */
    /* 当前页码。0 表示未进入全屏(读数归一为 -1);
     * >= 1 表示全屏中的实际估算页码。 */
    int page;
} pc_local_source_t;

/* get_page 虚表项的前向声明(定义在文件后部,以便先声明虚表与单例)。 */
static int local_get_page(const pc_page_source_t *s);

/* 本机估算器虚表(静态只读,生命周期为整个运行期)。 */
static const pc_page_source_api_t s_local_api = {
    local_get_page,
};

/* 本机估算器静态单例。初始状态:未进全屏,读数 -1。 */
static pc_local_source_t s_local = { { &s_local_api }, 0 };

/* 本机估算器的 get_page 实现(虚表项)。
 * 参数:
 *   s 页码来源基类指针。实现上本估算器是静态单例,状态在
 *     s_local 中;接口保留该参数仅为与虚表签名一致。
 * 返回值:页码(>= 1);未进全屏返回 -1。 */
static int local_get_page(const pc_page_source_t *s)
{
    (void)s; /* 状态在静态单例中,不依赖传入指针内容 */
    return s_local.page >= 1 ? s_local.page : -1;
}

int pc_page_get(const pc_page_source_t *s)
{
    /* 防御式:NULL 实例或缺失虚表统一降级为 -1。 */
    if (s == NULL || s->api == NULL || s->api->get_page == NULL) {
        return -1;
    }
    return s->api->get_page(s);
}

pc_page_source_t *pc_local_page_source(void)
{
    /* 返回静态单例的基类视图。 */
    return &s_local.base;
}

/* 判断传入实例是否为本机估算器单例;不是则忽略操作。
 * 这样把其它实现的实例误传给本地接口时不会产生未定义行为。 */
static bool is_local(const pc_page_source_t *s)
{
    return s == &s_local.base;
}

void pc_local_fullscreen_entered(pc_page_source_t *s)
{
    if (!is_local(s)) {
        return;
    }
    /* 进入全屏:页码复位为 1(规格 §1/FR-12)。 */
    s_local.page = 1;
}

void pc_local_step(pc_page_source_t *s, int delta)
{
    int next;

    if (!is_local(s)) {
        return;
    }
    /* 未进全屏时不计数(页码语义只存在于全屏演示场景)。 */
    if (s_local.page < 1) {
        return;
    }
    next = s_local.page + delta;
    /* 钳制:页码不可能小于 1(规格 §1/FR-12)。 */
    s_local.page = next >= 1 ? next : 1;
}

void pc_local_fullscreen_exited(pc_page_source_t *s)
{
    if (!is_local(s)) {
        return;
    }
    /* 退出全屏:停止计数,读数回到 -1。 */
    s_local.page = 0;
}
