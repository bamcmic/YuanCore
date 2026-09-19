/* pmm.h - 物理内存管理(帧分配器) */
#ifndef PMM_H
#define PMM_H

#include "multiboot.h"

#define PMM_FRAME 4096U

void         pmm_init(const struct multiboot_info *mbi);

/* 返回物理帧基址(4K 对齐)，耗尽返回 0 */
unsigned int pmm_alloc_frame(void);
void         pmm_free_frame(unsigned int addr);

unsigned int pmm_total_kb(void);
unsigned int pmm_free_kb(void);
unsigned int pmm_used_kb(void);

/* pmm_init() 预留给内核堆的物理区间，heap_init() 用它初始化分配器 */
void pmm_heap_region(unsigned int *base, unsigned int *size);

#endif /* PMM_H */
