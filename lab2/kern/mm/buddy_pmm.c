#include "buddy_pmm.h"
#include <list.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

unsigned *root;         // 堆的根节点
unsigned root_size;     // 根节点能保存的最大页数
struct Page *base_page; // 可分配页面实际开始地址（前面是堆）
int free_page_num;      // 空闲页数
int node_num;           // 堆的节点数
int heap_page_num;      // 存放堆的页数

// 函数声明
unsigned fix_up(unsigned num);//将给定的 num 向上舍入到最近的2的整数次幂。
unsigned fix_down(unsigned num);//将给定的 num 向下舍入到最近的2的整数次幂。
static inline unsigned LEFT_LEAF(unsigned index);
static inline unsigned RIGHT_LEAF(unsigned index);
static inline unsigned PARENT(unsigned index);
static inline unsigned MAX(unsigned a, unsigned b);
static inline unsigned MAX_LONGEST(unsigned index);
static void update(int index);

// 向上舍入到最近的2的整数次幂的幂
unsigned fix_up(unsigned num) {
    int power = 0;
    while ((1 << power) < num) {
        power++;
    }
    return power;
}

// 向下舍入到最近的2的整数次幂的幂
unsigned fix_down(unsigned num) {
    int power = 0;
    while ((1 << (power + 1)) <= num) {
        power++;
    }
    return power;
}

// 计算当前节点的左孩子节点在数组中的下标
static inline unsigned LEFT_LEAF(unsigned index) {
    return index << 1;
}

// 计算当前节点的右孩子节点在数组中的下标
static inline unsigned RIGHT_LEAF(unsigned index) {
    return (index << 1) + 1;
}

// 计算当前节点的父节点在数组中的下标
static inline unsigned PARENT(unsigned index) {
    return index >> 1;
}

// 计算 a 和 b 中的最大值
static inline unsigned MAX(unsigned a, unsigned b) {
    return (a > b) ? a : b;
}

// 计算某个index对应的最大longest
static inline unsigned MAX_LONGEST(unsigned index) {
    return root_size >> fix_down(index);
}

// 更新某个index对应的longest
static void update(int index) {
    int l_index = LEFT_LEAF(index);
    int r_index = RIGHT_LEAF(index);
    if (l_index < node_num) { // 存在左子树
        if (r_index < node_num) { // 存在右子树
            if (root[l_index] == root[r_index] && root[r_index] == MAX_LONGEST(r_index)) {
                root[index] = 2 * root[r_index]; // 将该节点的longest设为两者的和
            } else {
                root[index] = MAX(root[l_index], root[r_index]); // 将该节点的longest设为较大值
            }
        } else { // 只存在左子树
            root[index] = root[l_index];
        }
    }
}
// 在init时将free_page_num设为零
static void buddy_init(void)
{
    free_page_num = 0; // 初始化空闲页数
    memset(root, 0, sizeof(unsigned) * node_num); // 清空根节点数组
}

static void buddy_init_memmap(struct Page *base, size_t n)
{
   // 确保参数合法
    assert(n > 0);
    for (struct Page *p = base; p != base + n; p++) {
        assert(PageReserved(p)); // 判断当前页面是否是系统保留的
        // 清空当前页框的标志信息，并将页框的引用计数设置为0，将页面设为空闲态
        p->flags = 0;
        set_page_ref(p, 0);
        SetPageProperty(p);
    }

    // 构建二叉树存储不同节点的使用情况
    // 实际分配的大小，向上变为最近的2的整数次幂
    root_size = 1 << fix_up(n);
    // 二叉树数组的大小，从1开始编码，所以需要2倍的空间
    node_num = 2 * root_size;
    // 利用最开始的若干页建立二叉树组
    root = (unsigned *)KADDR(page2pa(base));
    // 得到二叉树组需要的页数
    int heap_page_size = node_num * sizeof(unsigned *);
    heap_page_num = heap_page_size / 4096 + (int)(heap_page_size % 4096 != 0);
    // 得到能分配的页的首地址
    base_page = base + heap_page_num;
    // 更新空闲页
    free_page_num += n - heap_page_num;

    // 由于申请的页数向向上变为最近的2的整数次幂，而系统输入的n为物理内存能容纳的最大页数，所以多申请的页数是非法的
    // 将多申请的部分的最大可分配页数设为0
    for (int i = root_size + free_page_num; i <= node_num; i++) {
        root[i] = 0;
    }
    // 将叶子节点的longest设置为1
    for (int i = root_size + free_page_num - 1; i >= root_size; i--) {
        root[i] = 1;
    }
    // 更新每个块的longest
    for (int i = root_size - 1; i > 0; i--) {
        update(i);
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);       // 确保请求的页面数大于0
    
    // 向上调整 n 为最近的2的整数次幂
    n = 1 << fix_up(n);
    
    // 检查是否有足够的空间
    if (n > root[1]) // 从根节点开始检查
        return NULL;

    unsigned index = 1;  // 从根节点开始遍历
    size_t node_size = root_size;

    // 深度优先遍历二叉树以找到适合的块
    while (node_size != n) {
        if (root[LEFT_LEAF(index)] >= n) { // 如果左子树足够大，向左子树申请
            index = LEFT_LEAF(index);
        } else { // 否则，向右子树申请
            index = RIGHT_LEAF(index);
        }
        node_size /= 2; // 更新当前节点的大小
    }

    // 计算对应的起始页面
    int left_brother_num = index - (1 << fix_down(index)); // 计算左兄弟节点数
    struct Page *base = base_page + (left_brother_num * n); // 计算开始的页面地址

    // 更新每个页面的状态
    for (struct Page *page = base; page != base + n; page++) {
        ClearPageProperty(page); // 清除页面的属性标志
    }

    // 更新堆中对应节点的longest
    root[index] = 0;    // 将找到的块取出分配
    free_page_num -= n; // 更新空闲页数

    // 向上回溯至根节点，修改沿途节点的大小
    while (PARENT(index)) {
        index = PARENT(index);
        update(index); // 更新每个父节点的longest
    }

    return base; // 返回分配的页面的起始地址
}


static void buddy_free_pages(struct Page *base, size_t n)
{
    assert(n > 0);      // 保证n大于零
    n = 1 << fix_up(n); // 将n向上变为最近的2的整数次幂

    // 检查对应page是否是保留态且已分配，将ref设为0
    for (struct Page *p = base; p < base + n; p++)
     {
        assert(!PageReserved(p) && !PageProperty(p));
        set_page_ref(p, 0);
    }

    // 对应叶节点索引
    unsigned index = root_size + (base - base_page);

    // 找到对应节点
    for (unsigned node_size = 1; node_size != n; node_size <<= 1) {
        index = PARENT(index);
        assert(index); // 防止index为0
    }

    root[index] = n;    // 将对应节点的longest设置为n
    free_page_num += n; // 更新空闲页数目

    // 回溯直到根节点，更改沿途值
    while (PARENT(index)) {
        index = PARENT(index);
        update(index);
    }
}

static size_t buddy_nr_free_pages(void)
{
    return free_page_num;
}

static void buddy_check(void)
{
    // 申请并多个单页面
    struct Page *p0, *p1, *p2, *p3, *p4;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    // 判断申请页面的地址是否相邻
    assert(p0 + 1 == p1 && p1 + 1 == p2);

    // 判断申请的页面的ref是否为0
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    // 判断申请页面的地址是否合法
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    // 释放页面
    free_page(p0);
    free_page(p1);
    free_page(p2);

    // 申请并释放多个页面，且将一个块拆开释放
    p1 = alloc_pages(512);
    p2 = alloc_pages(512);
    free_pages(p1, 256);
    free_pages(p2, 512);
    free_pages(p1 + 256, 256);

    // 在最前面申请若干页面，以测试块不再最前方时的正确性
    p0 = alloc_pages(1024);
    assert(p0 == p1);

    // 申请两块页面，释放前一块页面，再申请两个小的页面
    // 判断两个小页面是否在刚释放的位置生成
    p1 = alloc_pages(100);
    p2 = alloc_pages(64);
    assert(p1 + 128 == p2);
    free_pages(p1, 128);
    p3 = alloc_pages(64);
    assert(p1 + 128 == p2);
    p4 = alloc_pages(64);
    assert(p4 == p3 + 64 && p4 == p2 - 64);

    // 全部释放
    free_pages(p2, 64);
    free_pages(p4, 64);
    free_pages(p3, 64);
    free_pages(p0, 1024);
}

const struct pmm_manager buddy_pmm_manager =
 {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
