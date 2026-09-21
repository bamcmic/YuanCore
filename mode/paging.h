/* paging.h - 分页 */
#ifndef PAGING_H
#define PAGING_H

void paging_init(void);   /* 建页目录并开启 CR0.PG，之后不能再访问未映射区域 */

/* 把 [start, start+len) 设为内核只读（写它 → 页错误，panic 点名肇事 EIP）。
 * 必须在 paging_init 之后调用。 */
void paging_set_ro(unsigned int start, unsigned int len);

#endif /* PAGING_H */
