/* io.h - x86 端口读写内联汇编 */
#ifndef IO_H
#define IO_H

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    /* 往一个无害端口写一次，起短暂延时作用 */
    outb(0x80, 0);
}

static inline void cli(void)
{
    __asm__ volatile ("cli");
}

static inline void sti(void)
{
    __asm__ volatile ("sti");
}

static inline void hlt(void)
{
    __asm__ volatile ("hlt");
}

#endif /* IO_H */
