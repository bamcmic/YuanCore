/* paging.c - 分页
 *
 * 单页目录模型（同一时刻只有一个用户程序，且系统同步执行它）：
 *
 *   0x00000000-0x003FFFFF  内核镜像/位图/显存文本   supervisor RW
 *   0x00400000-0x007FFFFF  用户程序区 + 用户栈       user RW
 *   0x00800000-0x00BFFFFF  内核堆                    supervisor RW
 *   0xFC000000-0xFFFFFFFF  线性帧缓冲(QEMU 在 0xFD000000) supervisor RW
 *   0xFFC00000             递归映射页目录（访问任意页表用，暂未用）
 *
 * user 位就是保护边界：ring3 碰内核页 → 页错误 → 杀掉程序。
 * 页表本体都是内核 BSS 里的静态数组（在低 4M 内，天然已映射）。
 */
#include "paging.h"
#include "memmap.h"
#include "io.h"

#define PAGE_PRESENT 0x001u
#define PAGE_RW      0x002u
#define PAGE_USER    0x004u

#define PDE(idx) (idx)

static unsigned int page_dir[1024] __attribute__((aligned(4096)));
static unsigned int pt_low[1024]   __attribute__((aligned(4096)));
static unsigned int pt_user[1024]  __attribute__((aligned(4096)));
static unsigned int pt_heap[1024]  __attribute__((aligned(4096)));

/* 0xFC000000 起往上映射，覆盖 QEMU 的帧缓冲窗口(0xFD000000)。
 * 注意两点：
 *   1. PDE 下标 = VA >> 22，0xFC000000 >> 22 = 1008（曾误写成 252，
 *      帧缓冲页没映射，一画屏就页错误 → 黑屏）；
 *   2. PDE[1023] 是递归映射页目录，最多只能占到 1022，所以是 15 项。 */
#define FB_PTS 15
#define FB_PDE_BASE (0xFC000000u >> 22)
static unsigned int pt_fb[FB_PTS][1024] __attribute__((aligned(4096)));

static void fill_identity(unsigned int *pt, unsigned int base_pa,
                          unsigned int flags)
{
    unsigned int i;

    for (i = 0; i < 1024; i++)
        pt[i] = (base_pa + i * 4096u) | flags;
}

void paging_init(void)
{
    unsigned int i, f;

    fill_identity(pt_low,  0x00000000u, PAGE_PRESENT | PAGE_RW);
    fill_identity(pt_user, 0x00400000u, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    fill_identity(pt_heap, 0x00800000u, PAGE_PRESENT | PAGE_RW);

    for (f = 0; f < FB_PTS; f++)
        fill_identity(pt_fb[f], 0xFC000000u + f * 0x00400000u,
                      PAGE_PRESENT | PAGE_RW);

    for (i = 0; i < 1024; i++)
        page_dir[i] = 0;

    page_dir[0]   = ((unsigned int)pt_low)  | PAGE_PRESENT | PAGE_RW;
    page_dir[1]   = ((unsigned int)pt_user) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    page_dir[2]   = ((unsigned int)pt_heap) | PAGE_PRESENT | PAGE_RW;
    for (f = 0; f < FB_PTS; f++)
        page_dir[FB_PDE_BASE + f] = ((unsigned int)pt_fb[f])
                                    | PAGE_PRESENT | PAGE_RW;

    /* 递归映射：PDE[1023] 指向页目录自身 */
    page_dir[1023] = ((unsigned int)page_dir) | PAGE_PRESENT | PAGE_RW;

    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_dir));

    {
        unsigned int cr0;

        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000u;                 /* CR0.PG */
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    }
}
