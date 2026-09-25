/* x64/paging64.c - x86-64 页表维护
 *
 * 页表布局见 x64/boot32.asm:PML4[0] -> PDPT[0..3] -> PD[0..2047],
 * 2MB 大页恒等映射低 4GB。
 * 本文件只实现"设只读"陷阱(与 i386 版 paging_set_ro 同用途):
 * CR0.WP 已在 boot32.asm 开启,ring0 写只读页会页错误。
 */
#include "paging64.h"

#include <stdint.h>

#define P64_PRESENT 0x001ull
#define P64_RW      0x002ull

/* 拆 2MB 大页用的 4KB 页表(专用静态区,恒等映射,绝不复用栈/其他数据) */
static uint64_t split_pt[512] __attribute__((aligned(4096)));

/* boot32.asm 的页表(extern,物理地址 = 虚拟地址,恒等映射) */
extern uint8_t pml4[];          /* [bits 32] 段里定义,nasm 符号无类型 */
extern uint8_t pdpt[];
extern uint8_t pd[];

/* CR3 寄存器里存的是 PML4 的物理地址 = 虚拟地址(恒等映射) */
static inline uint64_t read_cr3(void)
{
    uint64_t v;

    __asm__ volatile ("movq %%cr3, %0" : "=r"(v));
    return v;
}

static inline void reload_cr3(void)
{
    __asm__ volatile ("movq %0, %%cr3" : : "r"(read_cr3()) : "memory");
}

/* 取第 level 级表(1=PML4, 2=PDPT, 3=PD)里 va 对应的下一级表物理地址 */
static uint64_t *next_table(uint64_t *table, uint64_t va, int shift)
{
    uint64_t entry = table[(va >> shift) & 0x1FF];

    return (uint64_t *)(unsigned long)(entry & ~0xFFFull);
}

void paging64_set_ro(uint64_t start, uint64_t len)
{
    uint64_t a, end = start + len;
    uint64_t *pml4t = (uint64_t *)(unsigned long)read_cr3();

    start &= ~0xFFFull;
    end = (end + 0xFFFull) & ~0xFFFull;

    for (a = start; a < end; a += 4096) {
        uint64_t *pdpt = next_table(pml4t, a, 39);
        uint64_t *pd;
        uint64_t *pt;

        if (pdpt == 0)
            continue;
        pd = next_table(pdpt, a, 30);
        if (pd == 0)
            continue;

        /* 我们的映射全是 2MB 大页 —— 目标是 4KB 页时需要把大页拆成
         * 4KB 页表。IDT 页必然落在某张大页里,拆它所在的 PD 项。 */
        {
            uint64_t entry = pd[(a >> 21) & 0x1FF];
            uint64_t *pt;
            uint64_t base;
            int i;

            if ((entry & P64_PRESENT) == 0)
                continue;
            if ((entry & (1ull << 7)) == 0) {   /* 已是 4KB 页 */
                pt = (uint64_t *)(unsigned long)(entry & ~0xFFFull);
                pt[(a >> 12) & 0x1FF] &= ~P64_RW;
                continue;
            }

            /* 拆 2MB -> 512 × 4KB(恒等映射,物理=虚拟,直接找低 4GB 内存)。
             * 页表用堆?此时尚未初始化堆 —— 用 boot32 的 pd 后面的空闲
             * 4KB 区:pd 有 2048 项 = 16KB,本只需覆盖 4GB,预留富余。 */
            base = (uint64_t)(unsigned long)split_pt;
            pt = (uint64_t *)(unsigned long)base;

            for (i = 0; i < 512; i++) {
                uint64_t pa = (entry & ~0xFFFull & ~((1ull << 21) - 1))
                            + (uint64_t)i * 4096ull;

                pt[i] = pa | P64_PRESENT | P64_RW;
            }

            pd[(a >> 21) & 0x1FF] = ((uint64_t)(unsigned long)pt)
                                    | P64_PRESENT | P64_RW;
        }

        pt = (uint64_t *)(unsigned long)(pd[(a >> 21) & 0x1FF] & ~0xFFFull);
        pt[(a >> 12) & 0x1FF] &= ~P64_RW;
    }

    reload_cr3();
}

/* 诊断:打印 va 在 PML4/PDPT/PD 三级的原始表项(串口) */
void paging64_walk_dump(uint64_t va)
{
    extern void ser_puts64(const char *s);
    extern void fmt_hex64(uint64_t v, char *out);

    uint64_t *pml4t = (uint64_t *)(unsigned long)read_cr3();
    uint64_t e0 = pml4t[(va >> 39) & 0x1FF];
    uint64_t *pdpt = (uint64_t *)(unsigned long)(e0 & ~0xFFFull);
    uint64_t e1 = pdpt ? pdpt[(va >> 30) & 0x1FF] : 0;
    uint64_t *pdt = (uint64_t *)(unsigned long)(e1 & ~0xFFFull);
    uint64_t e2 = pdt ? pdt[(va >> 21) & 0x1FF] : 0;
    char b[20];

    ser_puts64("\r\n[walk 0x");
    fmt_hex64(va, b);
    ser_puts64(b);
    ser_puts64("] cr3=0x");
    fmt_hex64(read_cr3(), b);
    ser_puts64(b);
    ser_puts64(" pml4=0x");
    fmt_hex64(e0, b);
    ser_puts64(b);
    ser_puts64(" pdpt=0x");
    fmt_hex64(e1, b);
    ser_puts64(b);
    ser_puts64(" pd=0x");
    fmt_hex64(e2, b);
    ser_puts64(b);
    ser_puts64("\r\n");
}
