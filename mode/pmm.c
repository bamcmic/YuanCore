/* pmm.c - 物理内存管理
 *
 * 以 4KB 帧为单位做位图分配。可用内存范围取自 GRUB 的 multiboot memory map。
 * 位图本身和内核堆都摆放在内核镜像结束(kernel_end)之后，这段区域
 * 初始化时整段标成已占用，之后帧分配只发位图之后的空闲帧。
 */
#include "pmm.h"
#include "multiboot.h"
#include "memmap.h"

/* 链接脚本导出的内核结束地址 */
extern unsigned char kernel_end;

#define MEM_CAP_MB 512U                      /* 位图上限，够用且省内存 */
#define MB (1024U * 1024U)

static unsigned char *bitmap;
static unsigned int   nframes;               /* 位图管理的帧总数 */
static unsigned int   first_free;            /* 第一个可能空闲的帧号 */
static unsigned int   free_count;
static unsigned int   total_kb;
static unsigned int   heap_base_v, heap_size_v;

/* multiboot memory map 项：size + addr + len + type */
struct mmap_entry {
    unsigned int    size;
    unsigned long long addr;
    unsigned long long len;
    unsigned int    type;
} __attribute__((packed));

#define MMAP_AVAILABLE 1U

static inline int bitmap_test(unsigned int i)
{
    return (bitmap[i >> 3] >> (i & 7)) & 1;
}

static inline void bitmap_set(unsigned int i)
{
    bitmap[i >> 3] |= (unsigned char)(1u << (i & 7));
}

static inline void bitmap_clear(unsigned int i)
{
    bitmap[i >> 3] &= (unsigned char)~(1u << (i & 7));
}

static void mark_range_used(unsigned long long start, unsigned long long end)
{
    unsigned int f, e, i;

    f = (unsigned int)(start / PMM_FRAME);
    e = (unsigned int)((end + PMM_FRAME - 1) / PMM_FRAME);
    if (e > nframes)
        e = nframes;

    for (i = f; i < e; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (free_count > 0)
                free_count--;
        }
    }
}

void pmm_init(const struct multiboot_info *mbi)
{
    unsigned long long highest = 0;
    unsigned int i;
    unsigned long long cursor;

    nframes = 0;
    free_count = 0;
    total_kb = 0;
    first_free = 0;
    bitmap = 0;
    heap_base_v = heap_size_v = 0;

    if (mbi == 0)
        return;

    /* 1) 找到可用内存的上界 */
    if ((mbi->flags & MBI_FLAG_MMAP) != 0 && mbi->mmap_addr != 0) {
        cursor = mbi->mmap_addr;
        while (cursor < (unsigned long long)mbi->mmap_addr + mbi->mmap_length) {
            const struct mmap_entry *e =
                (const struct mmap_entry *)(unsigned long)cursor;

            if (e->type == MMAP_AVAILABLE &&
                e->addr + e->len > highest)
                highest = e->addr + e->len;
            cursor += e->size + 4;
        }
    } else if ((mbi->flags & MBI_FLAG_MEM) != 0) {
        highest = 1ULL * MB + (unsigned long long)mbi->mem_upper * 1024ULL;
    }

    if (highest > (unsigned long long)MEM_CAP_MB * MB)
        highest = (unsigned long long)MEM_CAP_MB * MB;

    /* 2) 位图紧跟内核镜像 */
    bitmap  = (unsigned char *)(((unsigned long)&kernel_end + 3) & ~3UL);
    nframes = (unsigned int)(highest / PMM_FRAME);
    if (nframes == 0)
        return;

    for (i = 0; i < (nframes + 7) / 8; i++)
        bitmap[i] = 0xFF;                    /* 先全标占用，再放行可用段 */

    free_count = 0;
    total_kb = (unsigned int)(highest / 1024U);

    /* 3) 放行内存映射里标记为可用的段 */
    if ((mbi->flags & MBI_FLAG_MMAP) != 0 && mbi->mmap_addr != 0) {
        cursor = mbi->mmap_addr;
        while (cursor < (unsigned long long)mbi->mmap_addr + mbi->mmap_length) {
            const struct mmap_entry *e =
                (const struct mmap_entry *)(unsigned long)cursor;

            if (e->type == MMAP_AVAILABLE) {
                unsigned int f = (unsigned int)(e->addr / PMM_FRAME);
                unsigned int last = (unsigned int)((e->addr + e->len) / PMM_FRAME);
                for (i = f; i < last && i < nframes; i++) {
                    if (bitmap_test(i)) {
                        bitmap_clear(i);
                        free_count++;
                    }
                }
            }
            cursor += e->size + 4;
        }
    }

    /* 4) 划掉保留区：低 1M、内核+位图、程序加载区、系统调用页、内核堆 */
    {
        unsigned int bitmap_end =
            (unsigned int)(unsigned long)bitmap + (nframes + 7) / 8;

        heap_base_v = MEM_HEAP_BASE;
        heap_size_v = MEM_HEAP_SIZE;

        mark_range_used(0, 1ULL * MB);                          /* BIOS/显存 */
        mark_range_used(1ULL * MB, bitmap_end);                 /* 内核+位图 */
        mark_range_used(MEM_SYSCALL_BASE, MEM_SYSCALL_BASE + PMM_FRAME);
        mark_range_used(MEM_EXEC_BASE,                          /* 用户程序区 */
                        (unsigned long long)MEM_USER_STACK_TOP);
        mark_range_used(MEM_HEAP_BASE,
                        (unsigned long long)MEM_HEAP_BASE + MEM_HEAP_SIZE);
    }

    /* 找第一个空闲帧 */
    first_free = 0;
    while (first_free < nframes && bitmap_test(first_free))
        first_free++;
}

unsigned int pmm_alloc_frame(void)
{
    unsigned int i;

    for (i = first_free; i < nframes; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_count--;
            first_free = i + 1;
            return i * PMM_FRAME;
        }
    }
    return 0;
}

void pmm_free_frame(unsigned int addr)
{
    unsigned int i = addr / PMM_FRAME;

    if (addr == 0 || i >= nframes || !bitmap_test(i))
        return;
    bitmap_clear(i);
    free_count++;
    if (i < first_free)
        first_free = i;
}

unsigned int pmm_total_kb(void) { return total_kb; }
unsigned int pmm_free_kb(void)  { return free_count * (PMM_FRAME / 1024U); }
unsigned int pmm_used_kb(void)
{
    unsigned int t = total_kb;
    unsigned int f = free_count * (PMM_FRAME / 1024U);
    return (t > f) ? (t - f) : 0;
}

void pmm_heap_region(unsigned int *base, unsigned int *size)
{
    if (base) *base = heap_base_v;
    if (size) *size = heap_size_v;
}
