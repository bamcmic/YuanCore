/* gdt.h - 全局描述符表 */
#ifndef GDT_H
#define GDT_H

void gdt_init(void);
void gdt_install_tss(unsigned int base, unsigned short limit);

#endif /* GDT_H */
