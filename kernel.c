/* kernel.c - YuanCore 内核入口
 *
 * 启动顺序很关键：
 *   cli -> GDT -> IDT -> PIC -> 物理内存 -> 堆 -> 时钟/键盘 -> 画桌面 -> sti -> shell
 *
 * 一旦中断被打开，任何异常/IRQ 都会打进我们自己的 IDT，
 * 所以 IDT 必须先于 sti 就绪，否则直接三重故障。
 */
#include "mode/print.h"
#include "mode/multiboot.h"
#include "mode/fb.h"
#include "mode/font.h"
#include "mode/theme.h"
#include "mode/desktop.h"
#include "mode/gdt.h"
#include "mode/idt.h"
#include "mode/irq.h"
#include "mode/timer.h"
#include "mode/kbd.h"
#include "mode/pmm.h"
#include "mode/heap.h"
#include "mode/fs.h"
#include "mode/mouse.h"
#include "mode/tss.h"
#include "mode/paging.h"
#include "mode/idt.h"
#include "mode/io.h"
#include "shell/syscall.h"
#include "shell/shell.h"
#include "mode/app_hello_blob.h"
#include "mode/app_closeall_blob.h"
#include "shell/console.h"
#include "shell/shell.h"

#define MULTIBOOT_MAGIC 0x2BADB002

/* panic 时控制台可能还没就绪：图形模式画到屏幕上，文本模式写 0xB8000。
 * （教训：曾只在文本模式打 panic，图形模式下一出错就是无声黑屏。） */
#ifndef YC_X64
void fmt_hex(unsigned int v, char *out)
{
    static const char *d = "0123456789ABCDEF";
    int i;

    out[8] = '\0';
    for (i = 7; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
}

void print_panic(const struct registers *r)
{
    static const char *names[32] = {
        "Divide by Zero",          "Debug",
        "Non-Maskable Interrupt",  "Breakpoint",
        "Overflow",                "Out of Bounds",
        "Invalid Opcode",          "No Coprocessor",
        "Double Fault",            "Coprocessor Segment Overrun",
        "Invalid TSS",             "Segment Not Present",
        "Stack Fault",             "General Protection Fault",
        "Page Fault",              "Unknown Interrupt",
        "x87 FPU Error",           "Alignment Check",
        "Machine Check",           "SIMD FP Exception",
        "Virtualization Exception","Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved",
        "Reserved",                "Reserved"
    };
    unsigned int cr2;

    __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));

    if (fb_ready()) {
        unsigned int white = RGB(0xFF, 0xFF, 0xFF);
        int x = 40, y = 56;
        char b[16];

        fb_fillrect(0, 0, (int)fb.width, (int)fb.height,
                    RGB(0x7A, 0x10, 0x10));
        font_drawtext(x, y, "*** YuanCore PANIC ***", white, 3);
        y += 60;

        font_drawtext(x, y, "exception: ", white, 2);
        font_drawtext(x + 200, y, names[r->int_no & 31], white, 2);
        y += 36;

        /* 两列一组：cr2/err, eip/cs, eflags/ss */
        {
            int cx2[2];

            cx2[0] = x;
            cx2[1] = x + 480;

            font_drawtext(cx2[0], y, "cr2    : 0x", white, 2);
            fmt_hex(cr2, b);
            font_drawtext(cx2[0] + 200, y, b, white, 2);
            font_drawtext(cx2[1], y, "err    : 0x", white, 2);
            fmt_hex(r->err_code, b);
            font_drawtext(cx2[1] + 200, y, b, white, 2);
            if (r->int_no == 14) {
                /* 页错误码解码：bit1=写，bit2=用户态 */
                const char *acc = (r->err_code & 2) ? "WRITE" : "READ";
                const char *who = (r->err_code & 4) ? "USER" : "KERNEL";

                font_drawtext(cx2[1] + 340, y, acc, white, 2);
                font_drawtext(cx2[1] + 460, y, who, white, 2);
            }
            y += 30;

            font_drawtext(cx2[0], y, "eip    : 0x", white, 2);
            fmt_hex(r->eip, b);
            font_drawtext(cx2[0] + 200, y, b, white, 2);
            font_drawtext(cx2[1], y, "cs     : 0x", white, 2);
            fmt_hex(r->cs, b);
            font_drawtext(cx2[1] + 200, y, b, white, 2);
            y += 30;

            font_drawtext(cx2[0], y, "eflags : 0x", white, 2);
            fmt_hex(r->eflags, b);
            font_drawtext(cx2[0] + 200, y, b, white, 2);
            y += 40;
        }

        /* 通用寄存器 + 段寄存器 + 用户栈指针：污染源就在这些值里 */
        {
            struct reg_row {
                const char *name;
                unsigned int v;
            } rows[13];
            int i, k;

            rows[0].name = "eax";  rows[0].v = r->eax;
            rows[1].name = "ebx";  rows[1].v = r->ebx;
            rows[2].name = "ecx";  rows[2].v = r->ecx;
            rows[3].name = "edx";  rows[3].v = r->edx;
            rows[4].name = "esi";  rows[4].v = r->esi;
            rows[5].name = "edi";  rows[5].v = r->edi;
            rows[6].name = "ebp";  rows[6].v = r->ebp;
            rows[7].name = "uesp"; rows[7].v = r->user_esp;
            rows[8].name = "ds ";  rows[8].v = r->ds;
            rows[9].name = "es ";  rows[9].v = r->es;
            rows[10].name = "fs "; rows[10].v = r->fs;
            rows[11].name = "gs "; rows[11].v = r->gs;
            rows[12].name = "ss "; rows[12].v = r->ss;

            for (i = 0; i < 13; i++) {
                int lx = x + (i % 4) * 240;
                int ly = y + (i / 4) * 28;

                font_drawtext(lx, ly, rows[i].name, white, 2);
                fmt_hex(rows[i].v, b);
                font_drawtext(lx + 80, ly, b, white, 2);
            }
            y += 3 * 28 + 12;
            for (k = 0; k < 13 % 4; k++)
                ;
        }

        /* 内核栈顶 8 个字：栈上残留的返回地址能指认肇事函数 */
        {
            unsigned int *sp;
            int i, k;

            __asm__ volatile ("movl %%esp, %0" : "=r"(sp));
            font_drawtext(x, y, "stack:", white, 2);
            y += 30;
            for (i = 0; i < 8; i++) {
                fmt_hex(sp[i], b);
                k = i % 4;
                font_drawtext(x + k * 240, y, b, white, 2);
                if (k == 3)
                    y += 30;
            }
        }

        font_drawtext(x, y + 6, "System halted. Take a photo for debugging.",
                      white, 2);
        return;
    }

    vga_clear();
    vga_color(0x4F);                       /* 红底白字 */
    printf("*** YuanCore PANIC: exception %d (%s) ***\n\n",
           (int)r->int_no, names[r->int_no & 31]);
    printf("cr2        : 0x%X\n", cr2);
    printf("error code : 0x%X\n", r->err_code);
    printf("eip        : 0x%X\n", r->eip);
    printf("cs         : 0x%X\n", r->cs);
    printf("eflags     : 0x%X\n", r->eflags);
    printf("\nSystem halted.\n");
}
#endif /* !YC_X64 */

#ifndef YC_X64
void kernel_main_common(const struct multiboot_info *mbi);

void kmain(unsigned int magic, unsigned int mbi_addr)
{
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)mbi_addr;

    cli();                                 /* 装好 IDT 之前绝不能开中断 */

    if (magic != MULTIBOOT_MAGIC) {
        vga_clear();
        vga_color(0x0F);
        printf("YuanCore: bad multiboot magic 0x%X\n", magic);
        for (;;)
            hlt();
    }

    kernel_main_common(mbi);               /* 不返回 */
}
#endif /* !YC_X64 */

/* 初始化与运行主流程:i386 入口(kmain)与 x86-64 入口(kernel64.c 的
 * kmain64)在各自解析完引导信息后都汇入这里 —— 保证两条架构路径
 * 的初始化顺序永远一致。 */
void kernel_main_common(const struct multiboot_info *mbi)
{
    int rc;
#if defined(YC_X64)
    extern void gdt64_init(void);
    extern void idt64_init(void);
    extern void paging64_set_ro(unsigned long long start,
                                unsigned long long len);
    extern unsigned long long idt64_page_base(void);
    extern void pic64_init(void);
    extern void ser_puts64(const char *s);   /* kernel64.c:COM1 信标 */
#else
    extern unsigned int idt_page_base(void);
#endif

#if defined(YC_X64)
    gdt64_init();
    ser_puts64(" G");
    idt64_init();
    ser_puts64(" I");
    paging64_set_ro(idt64_page_base(), 4096);  /* IDT 只读陷阱 */
    pic64_init();
    ser_puts64(" C");
#else
    gdt_init();
    tss_init();                            /* ring3 中断切内核栈 */
    idt_init();
    paging_init();                         /* 开分页：此后只碰已映射区域 */
    paging_set_ro(idt_page_base(), 4096);  /* IDT 只读：写它 → 页错误点名肇事者 */
    irq_init();                            /* 重映射 PIC，只放行 IRQ0/1/2/12 */
#endif

    pmm_init(mbi);
    heap_init();
    fs_init();                             /* 需要 kmalloc，必须在 heap_init 之后 */

    /* 预置示例文件与演示程序（内存文件系统，重启回初始状态） */
    fs_create_dir("/apps");
    fs_create_dir("/docs");
    fs_write("/readme.txt",
             "YuanCore ramfs\nFiles here live in memory only.\n", 48);
    fs_write("/docs/about.txt",
             "YuanCore - 32-bit x86 hobby kernel.\n", 37);
#ifndef YC_X64
    /* x86-64 版不迁移 ring3，不内置 .app(用户程序为 ELF32) */
    fs_write("/apps/hello.app", app_hello_blob, APP_HELLO_LEN);
    fs_write("/apps/closeall.app", app_closeall_blob, APP_CLOSEALL_LEN);
#endif

    kbd_init();
    mouse_init();                          /* PS/2 鼠标，IRQ12 */
    timer_init(100);                       /* 100Hz，顺带验证中断链路 */

#if defined(YC_X64)
    ser_puts64(" f");                      /* 即将初始化帧缓冲 */
#endif
    rc = fb_init(mbi);
    if (rc != 0) {
#if defined(YC_X64)
        ser_puts64(" FB-FAIL\r\n");
        sti();
#endif
        vga_clear();
        vga_color(0x0F);
        printf("YuanCore - %s kernel\n\n",
               (sizeof(void *) == 8) ? "x86-64" : "i386");
        printf("Framebuffer unavailable, fb_init() = %d\n", rc);
        printf("  mbi flag : 0x%X\n", (unsigned int)mbi->flags);
        printf("  fb type  : %d, bpp %d\n",
               (int)mbi->framebuffer_type, (int)mbi->framebuffer_bpp);
        printf("\nFalling back to VGA text mode.\n");
        for (;;)
            hlt();
    }
#if defined(YC_X64)
    ser_puts64(" F\r\n");                  /* 帧缓冲就绪,开始画桌面 */
    {
        extern void paging64_walk_dump(unsigned long long va);
        paging64_walk_dump(0xFD000000ull); /* 帧缓冲三级表项诊断 */
        paging64_walk_dump(0x102000ull);   /* 内核代码区对照 */
    }
#endif

    /* ---- 图形路径 ---- */
    theme_init();
#if defined(YC_X64)
    ser_puts64(" t");
#endif
    shell_install_clock();                 /* 时钟提供者必须先于首帧注册 */
#if defined(YC_X64)
    ser_puts64(" k");
#endif
    desktop_draw(mbi);
#if defined(YC_X64)
    ser_puts64(" d");                      /* 桌面已画完 */
#endif

    syscall_init();                        /* 程序用的服务表(固定地址) */
#ifndef YC_X64
    isr_register(0x80, syscall_dispatch);  /* 系统调用门已在 IDT 里开 DPL3 */
#endif

    console_init(SHELL_WIN_X + SHELL_PAD,
                 SHELL_WIN_Y + 36 + SHELL_PAD,      /* 36 = 标题栏高 */
                 SHELL_WIN_W - 2 * SHELL_PAD,
                 SHELL_WIN_H - 36 - 22 - 2 * SHELL_PAD);  /* 22 = 状态条高 */
#if defined(YC_X64)
    ser_puts64(" c");
#endif

    {
        void *probe = kmalloc(64);
        if (probe != 0) {
            kfree(probe);
            console_puts("[ok ] interrupts, PMM, heap online\n");
        } else {
            console_puts("[err] kernel heap is not working\n");
        }
    }
    console_puts("[ok ] type 'help' for commands\n\n");

    sti();
#if defined(YC_X64)
    ser_puts64(" S");                      /* 中断已开 */
#endif

    /* 开机自检：等 200ms 看时钟 tick 是否前进。
     * 不动 = IRQ0 没进来（PIC/EOI/IDT 链路有问题），键盘必然也不响。 */
    {
        unsigned int t0 = timer_ticks();

        while (timer_ticks() - t0 < 20)
            hlt();

        if (timer_ticks() - t0 >= 20)
            console_puts("[ok ] timer IRQ0 alive (20 ticks / 200ms)\n");
        else
            console_puts("[err] timer IRQ0 dead - interrupt path broken\n");
    }

    shell_init();
    shell_run();                           /* 不返回 */
}
