/* x64/paging64.h - x86-64 四级页表 */
#ifndef X64_PAGING64_H
#define X64_PAGING64_H

#include <stdint.h>

/* 页表本体由 boot32.asm 的桩构建(恒等映射低 4GB,2MB 大页),
 * CR0.PG 在进入 kmain64 之前已开启,这里只提供后续维护接口。 */

/* 把 [start, start+len) 设为内核态只读(踩内存陷阱)。
 * 要求已按页对齐;效果与 i386 版 paging_set_ro 一致。 */
void paging64_set_ro(uint64_t start, uint64_t len);

#endif /* X64_PAGING64_H */
