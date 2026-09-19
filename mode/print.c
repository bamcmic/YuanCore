/* print.c - VGA 文本模式输出实现
 *
 * 显存布局：0xB8000 起，每字符 2 字节（低字节 ASCII，高字节属性），共 80x25。
 */
#include "print.h"
#include <stdarg.h>

static volatile unsigned short *const vga_buf =
    (volatile unsigned short *)VGA_BASE;

static unsigned char g_attr = 0x0F;   /* 默认黑底白字 */
static unsigned int  g_pos  = 0;      /* 线性光标位置 0..1999 */

void vga_color(unsigned char attr)
{
    g_attr = attr;
}

static void vga_put(unsigned int pos, char c)
{
    vga_buf[pos] = (unsigned short)(((unsigned short)g_attr << 8)
                                    | (unsigned char)c);
}

/* 整体上滚一行，末行填空格 */
static void vga_scroll(void)
{
    unsigned int i;

    for (i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++)
        vga_buf[i] = vga_buf[i + VGA_WIDTH];
    for (; i < VGA_SIZE; i++)
        vga_put(i, ' ');

    g_pos = VGA_WIDTH * (VGA_HEIGHT - 1);
}

void vga_clear(void)
{
    unsigned int i;

    for (i = 0; i < VGA_SIZE; i++)
        vga_put(i, ' ');
    g_pos = 0;
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        g_pos = (g_pos / VGA_WIDTH + 1) * VGA_WIDTH;
        break;
    case '\r':
        g_pos = (g_pos / VGA_WIDTH) * VGA_WIDTH;
        break;
    case '\t':
        g_pos = (g_pos + 8) & ~(unsigned int)7;
        break;
    default:
        vga_put(g_pos++, c);
        break;
    }

    if (g_pos >= VGA_SIZE)
        vga_scroll();
}

static void vga_putuint(unsigned int v, unsigned int base, int upper)
{
    char buf[16];
    int i = 0;

    if (v == 0) {
        vga_putc('0');
        return;
    }

    while (v > 0) {
        unsigned int d = v % base;
        buf[i++] = (d < 10)
                     ? (char)('0' + d)
                     : (char)((upper ? 'A' : 'a') + d - 10);
        v /= base;
    }

    while (i > 0)
        vga_putc(buf[--i]);
}

static void vga_putint(int v)
{
    if (v < 0) {
        vga_putc('-');
        vga_putuint((unsigned int)(-(unsigned int)v), 10, 0);
    } else {
        vga_putuint((unsigned int)v, 10, 0);
    }
}

void printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            vga_putc(*fmt++);
            continue;
        }

        fmt++;
        switch (*fmt) {
        case 'c':
            vga_putc((char)va_arg(ap, int));
            fmt++;
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == 0)
                s = "(null)";
            while (*s)
                vga_putc(*s++);
            fmt++;
            break;
        }
        case 'd':
        case 'i':
            vga_putint(va_arg(ap, int));
            fmt++;
            break;
        case 'u':
            vga_putuint(va_arg(ap, unsigned int), 10, 0);
            fmt++;
            break;
        case 'x':
            vga_putuint(va_arg(ap, unsigned int), 16, 0);
            fmt++;
            break;
        case 'X':
            vga_putuint(va_arg(ap, unsigned int), 16, 1);
            fmt++;
            break;
        case 'p':
            vga_putc('0');
            vga_putc('x');
            vga_putuint(va_arg(ap, unsigned int), 16, 0);
            fmt++;
            break;
        case '%':
            vga_putc('%');
            fmt++;
            break;
        default:
            vga_putc('%');
            vga_putc(*fmt++);
            break;
        }
    }
    va_end(ap);
}
