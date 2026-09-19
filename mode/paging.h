/* paging.h - 分页 */
#ifndef PAGING_H
#define PAGING_H

void paging_init(void);   /* 建页目录并开启 CR0.PG，之后不能再访问未映射区域 */

#endif /* PAGING_H */
