/* heap.c - 内核堆分配器
 *
 * 首次适配的空闲链表。块头部带大小与占用标志，空闲块按地址合并，
 * 防止反复分配释放把堆切成碎片。中断关闭做临界区，避免被时钟打断。
 */
#include "heap.h"
#include "pmm.h"
#include "io.h"

struct block {
    unsigned int size;          /* 不含头部 */
    unsigned int used;
    struct block *next;
    struct block *prev;
};

#define BLOCK_HDR  sizeof(struct block)
#define ALIGN4(x)  (((x) + 3U) & ~3U)
#define MIN_SPLIT  32U

static struct block *free_list;
static unsigned char *heap_start;
static unsigned int   heap_size;
static unsigned int   heap_used;

void heap_init(void)
{
    unsigned int base = 0, size = 0;

    pmm_heap_region(&base, &size);

    heap_start = (unsigned char *)(unsigned long)base;
    heap_size  = size;
    heap_used  = 0;

    free_list = (struct block *)heap_start;
    free_list->size = size - BLOCK_HDR;
    free_list->used = 0;
    free_list->next = 0;
    free_list->prev = 0;
}

/* 中断临界区辅助:i386 与 x86-64 的 EFLAGS 压栈指令不同 */
static inline unsigned int irq_save(void)
{
    unsigned long f;
#if defined(__x86_64__)
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f));
#else
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(f));
#endif
    return (unsigned int)f;
}

void *kmalloc(unsigned int size)
{
    struct block *b;
    unsigned int flags;
    void *p = 0;

    if (size == 0 || heap_size == 0)
        return 0;

    size = ALIGN4(size);

    /* 堆可能在中断里被用，做个简单临界区 */
    flags = irq_save();

    for (b = free_list; b != 0; b = b->next) {
        if (b->used)
            continue;
        if (b->size < size)
            continue;

        /* 剩余空间足够大才切，否则整块给出 */
        if (b->size >= size + BLOCK_HDR + MIN_SPLIT) {
            struct block *rest = (struct block *)
                ((unsigned char *)b + BLOCK_HDR + size);

            rest->size = b->size - size - BLOCK_HDR;
            rest->used = 0;
            rest->prev = b;
            rest->next = b->next;
            if (rest->next)
                rest->next->prev = rest;

            b->size = size;
            b->next = rest;
        }

        b->used = 1;
        heap_used += b->size + BLOCK_HDR;
        p = (unsigned char *)b + BLOCK_HDR;
        break;
    }

    if (flags & 0x200)          /* 之前是开中断的就恢复 */
        sti();

    return p;
}

void kfree(void *ptr)
{
    struct block *b;
    unsigned int flags;

    if (ptr == 0 || heap_size == 0)
        return;

    b = (struct block *)((unsigned char *)ptr - BLOCK_HDR);

    flags = irq_save();

    if ((unsigned char *)ptr - BLOCK_HDR < heap_start ||
        (unsigned char *)ptr > heap_start + heap_size) {
        if (flags & 0x200)
            sti();
        return;
    }

    b->used = 0;
    heap_used -= b->size + BLOCK_HDR;

    /* 与后块合并 */
    if (b->next && !b->next->used) {
        b->size += b->next->size + BLOCK_HDR;
        b->next = b->next->next;
        if (b->next)
            b->next->prev = b;
    }

    /* 与前块合并 */
    if (b->prev && !b->prev->used) {
        b->prev->size += b->size + BLOCK_HDR;
        b->prev->next = b->next;
        if (b->next)
            b->next->prev = b->prev;
    }

    if (flags & 0x200)
        sti();
}

unsigned int heap_used_bytes(void)  { return heap_used; }
unsigned int heap_total_bytes(void) { return heap_size; }
