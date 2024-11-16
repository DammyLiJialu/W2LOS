#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_lru.h>
#include <list.h>

list_entry_t pra_list_head3;

static int
_lru_init_mm(struct mm_struct *mm)
{     

    list_init(&pra_list_head3);
    mm->sm_priv = &pra_list_head3;
     //cprintf(" mm->sm_priv %x in fifo_init_mm\n",mm->sm_priv);
     return 0;
}

static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)//例如首次加载到内存
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;//从 mm（内存管理结构）的私有字段 sm_priv 获取链表头指针
    list_entry_t *entry=&(page->pra_page_link);//获取页面的链表节点指针。
 
    assert(entry != NULL && head != NULL);
    list_add((list_entry_t*) mm->sm_priv,entry);//将页面的节点插入到链表头部。
    return 0;
}
static int
_lru_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)//选择一个受害页面进行交换
{
     list_entry_t *head=(list_entry_t*) mm->sm_priv;
        assert(head != NULL);
    assert(in_tick==0);
    list_entry_t* entry = list_prev(head);// 获取链表中的最后一个页面
    if (entry != head) {
        list_del(entry);//// 删除该页面
        *ptr_page = le2page(entry, pra_page_link);//// 设置受害页面
    } else {
        *ptr_page = NULL;
    }
    return 0;
}
static void
print_mm_list() {
    cprintf("--------begin----------\n");
    list_entry_t *head = &pra_list_head3, *le = head;
    while ((le = list_next(le)) != head)
    {
        struct Page* page = le2page(le, pra_page_link);
        cprintf("vaddr: 0x%x\n", page->pra_vaddr);
    }
    cprintf("---------end-----------\n");
}
static int
_lru_check_swap(void) {
    print_mm_list();
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c; // 写入虚拟地址0x3000
    print_mm_list();
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b; // 写入虚拟地址0x2000
    print_mm_list();
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e; // 写入虚拟地址0x5000
    print_mm_list();
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a; // 写入虚拟地址0x1000
    print_mm_list();
    return 0;
}



static int
_lru_init(void)
{
    return 0;
}

static int
_lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_lru_tick_event(struct mm_struct *mm)
{ return 0; }

static int
unable_page_read(struct mm_struct *mm) {//该函数将链表中的每个页面标记为不可读（通过清除页表中的 PTE_R 位）。这用于在页面发生缺页中断时将所有页面设置为不可读
    list_entry_t *head=(list_entry_t*) mm->sm_priv, *le = head;
    while ((le = list_prev(le)) != head)
    {
        struct Page* page = le2page(le, pra_page_link);
        pte_t* ptep = NULL;
        ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
        *ptep &= ~PTE_R;
    }
    return 0;
}

int lru_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr) {///页面错误处理
    cprintf("lru page fault at 0x%x\n", addr);
    // 设置所有页面不可读
    if(swap_init_ok) 
        unable_page_read(mm);
    // 将需要获得的页面设置为可读
    pte_t* ptep = NULL;
    ptep = get_pte(mm->pgdir, addr, 0);
    *ptep |= PTE_R;
    if(!swap_init_ok) 
        return 0;
    struct Page* page = pte2page(*ptep);
    // 将该页放在链表头部
    list_entry_t *head=(list_entry_t*) mm->sm_priv, *le = head;
    while ((le = list_prev(le)) != head)
    {
        struct Page* curr = le2page(le, pra_page_link);
        if(page == curr) {
            
            list_del(le);//// 删除页面
            list_add(head, le);// 将页面放到链表头部
            break;
        }
    }
    return 0;
}

struct swap_manager swap_manager_lru =
{
    .name            = "lru swap manager",
    .init            = &_lru_init,
    .init_mm         = &_lru_init_mm,
    .tick_event      = &_lru_tick_event,
    .map_swappable   = &_lru_map_swappable,
    .set_unswappable = &_lru_set_unswappable,
    .swap_out_victim = &_lru_swap_out_victim,
    .check_swap      = &_lru_check_swap,
};

