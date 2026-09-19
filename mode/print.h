/* print.h - VGA 文本模式输出接口
 *
 * 头文件只放声明，不放定义：否则被多个 .c 包含时会产生重复符号。
 */
#ifndef PRINT_H
#define PRINT_H

#define VGA_BASE   0xB8000      /* VGA 文本缓冲区物理地址 */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_SIZE   (VGA_WIDTH * VGA_HEIGHT)

/* 设置字符属性：高 4 位背景色，低 4 位前景色（如 0x0F = 黑底白字） */
void vga_color(unsigned char attr);

/* 清屏，使用当前属性填充 */
void vga_clear(void);

/* 在当前光标处输出单个字符，支持 \n \r \t */
void vga_putc(char c);

/* 简易 printk，支持 %c %s %d %i %u %x %X %p %% */
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* PRINT_H */
