#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>
#include <memlayout.h>
#include "slub_pmm.h"
#include <stdio.h>

#define SLAB_SIZE PGSIZE   // 一页大小的 slab
#define MAX_PAGES 1024  // 根据系统需求设定
// 定义 le2slab 宏，用于将链表节点转换为 slab 结构
#define le2slab(le, member) \
    ((struct slab *)((char *)(le) - offsetof(struct slab, member)))

// 提前声明 slub_alloc_pages 和 page2kva
struct Page *slub_alloc_pages(size_t num_pages);
void *page2kva(struct Page *page);

// slab 结构体定义
struct slab {
    struct list_entry slab_list;  // 链表节点
    unsigned int free_count;      // 空闲对象计数
    void *free_ptr;               // 下一个可分配对象的指针
    void *start_addr;             // slab 的起始地址
};

// kmem_cache 结构体定义
struct kmem_cache {
    size_t size;                   // 缓存块大小
    size_t align;                  // 对齐大小
    struct list_entry slabs_full;   // 已满的 slab 链表
    struct list_entry slabs_partial;// 部分空闲的 slab 链表
    struct list_entry slabs_free;   // 全部空闲的 slab 链表
};

// 从 cache 中分配一个对象
void *kmem_cache_alloc(struct kmem_cache *cache) {
    struct slab *slab = NULL;

    // 检查部分空闲的 slab 链表
    if (!list_empty(&cache->slabs_partial)) {
        slab = le2slab(list_next(&cache->slabs_partial), slab_list);
    }
    // 如果部分空闲的 slab 不存在，尝试从全部空闲的 slab 中分配
    else if (!list_empty(&cache->slabs_free)) {
        slab = le2slab(list_next(&cache->slabs_free), slab_list);
        list_del(&slab->slab_list);            // 将该 slab 从 slabs_free 链表中移除
        list_add(&cache->slabs_partial, &slab->slab_list);  // 添加到 slabs_partial
    }
    // 如果没有可用 slab，则分配新的 slab
    else {
        struct Page *new_page = slub_alloc_pages(1);  // 分配一个新页
        if (new_page == NULL) {
            return NULL;  // 分配失败
        }

        // 初始化新 slab
        slab = (struct slab *)page2kva(new_page);  // 获取页面的虚拟地址
        slab->start_addr = slab;
        slab->free_ptr = (char *)slab + sizeof(struct slab);  // 对象起始位置
        slab->free_count = (SLAB_SIZE - sizeof(struct slab)) / cache->size;
        list_add(&cache->slabs_partial, &slab->slab_list);  // 添加到 slabs_partial
    }

    // 分配对象并更新 slab 状态
    void *obj = slab->free_ptr;
    slab->free_ptr = (char *)slab->free_ptr + cache->size;
    slab->free_count--;

    // 如果 slab 已满，移动到 slabs_full 链表
    if (slab->free_count == 0) {
        list_del(&slab->slab_list);
        list_add(&cache->slabs_full, &slab->slab_list);
    }
    return obj;
}


// 释放一个对象并归还到 cache
void kmem_cache_free(struct kmem_cache *cache, void *obj) {
    struct slab *slab = (struct slab *)((uintptr_t)obj & ~(SLAB_SIZE - 1));  // 获取对象所属的 slab
    slab->free_count++;
    
    // 如果 slab 原来是满的，将其从 slabs_full 移动到 slabs_partial
    if (slab->free_count == 1) {
        list_del(&slab->slab_list);
        list_add(&cache->slabs_partial, &slab->slab_list);
    }

    // 如果 slab 现在完全空闲，将其从 slabs_partial 移动到 slabs_free
    if (slab->free_count == (SLAB_SIZE - sizeof(struct slab)) / cache->size) {
        list_del(&slab->slab_list);
        list_add(&cache->slabs_free, &slab->slab_list);
    }
}

// 初始化内存映射
void slub_init_memmap(struct Page *base, size_t n) {
    for (size_t i = 0; i < n; i++) {
        SetPageReserved(base + i);
    }
    cprintf("SLUB memory map initialized.\n");
}

// 初始化 SLUB 分配器
void slub_init(void) {
    static struct kmem_cache cache;  // 声明一个 kmem_cache 类型的变量
    
    // 初始化 kmem_cache 的链表
    list_init(&cache.slabs_full);
    list_init(&cache.slabs_partial);
    list_init(&cache.slabs_free);

    // 初始化内存映射
    struct Page *base = alloc_pages(MAX_PAGES); // 使用适当的页面分配函数
    if (base == NULL) {
        cprintf("SLUB: Failed to allocate memory for initialization.\n");
        return;
    }
    slub_init_memmap(base, MAX_PAGES); // 初始化内存映射

    cprintf("SLUB: Allocator initialized with %d pages.\n", MAX_PAGES);
}



// 分配页
struct Page *slub_alloc_pages(size_t num_pages) {
    struct Page *page = alloc_pages(num_pages);
    if (page == NULL) {
        cprintf("SLUB: Failed to allocate %zu pages.\n", num_pages);
        return NULL;
    }
    cprintf("SLUB: Allocated %zu pages at address %p.\n", num_pages, page);
    return page;
}

// 释放页
void slub_free_pages(struct Page *page, size_t num_pages) {
    free_pages(page, num_pages);
    cprintf("SLUB: Freed %zu pages at address %p.\n", num_pages, page);
}

// 获取空闲页面数量
size_t slub_nr_free_pages(void) {
    size_t free_pages_count = nr_free_pages();
    cprintf("SLUB: Number of free pages: %zu.\n", free_pages_count);
    return free_pages_count;
}


static void slub_check(void) {
    cprintf("SLUB: Starting self-check...\n");

    // 1. 记录当前空闲页面数量
    size_t free_pages_start = slub_nr_free_pages();

    // 2. 分配一页并检查
    struct Page *page = slub_alloc_pages(1);
    assert(page != NULL);

    // 3. 检查分配后空闲页面计数是否减少
    size_t free_pages_after_alloc = slub_nr_free_pages();
    assert(free_pages_after_alloc == free_pages_start - 1);

    // 4. 释放分配的页面并检查
    slub_free_pages(page, 1);
    size_t free_pages_after_free = slub_nr_free_pages();
    assert(free_pages_after_free == free_pages_start);

    // 5. 检查分配超过最大页数的错误处理
    struct Page *oversized_alloc = slub_alloc_pages(free_pages_start + 1);
    assert(oversized_alloc == NULL);

}

// 定义 pmm_manager 结构体
const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager",
    .init = slub_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = slub_alloc_pages,
    .free_pages = slub_free_pages,
    .nr_free_pages = slub_nr_free_pages,
    .check = slub_check,
};
