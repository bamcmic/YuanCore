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
#include "mode/io.h"
#include "shell/syscall.h"
#include "shell/shell.h"
#include "mode/app_hello_blob.h"
#include "shell/console.h"
#include "shell/shell.h"

#define MULTIBOOT_MAGIC 0x2BADB002

/* panic 时控制台可能还没就绪：图形模式画到屏幕上，文本模式写 0xB8000。
 * （教训：曾只在文本模式打 panic，图形模式下一出错就是无声黑屏。） */
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

    if (fb_ready()) {
        unsigned int white = RGB(0xFF, 0xFF, 0xFF);
        int x = 60, y = 80;
        char b[16];

        fb_fillrect(0, 0, (int)fb.width, (int)fb.height,
                    RGB(0x7A, 0x10, 0x10));
        font_drawtext(x, y, "*** YuanCore PANIC ***", white, 3);
        y += 64;

        font_drawtext(x, y, "exception: ", white, 2);
        font_drawtext(x + 160, y, names[r->int_no & 31], white, 2);
        y += 40;

        font_drawtext(x, y, "error code : 0x", white, 2);
        fmt_hex(r->err_code, b);
        font_drawtext(x + 220, y, b, white, 2);
        y += 32;

        font_drawtext(x, y, "eip        : 0x", white, 2);
        fmt_hex(r->eip, b);
        font_drawtext(x + 220, y, b, white, 2);
        y += 32;

        font_drawtext(x, y, "cs         : 0x", white, 2);
        fmt_hex(r->cs, b);
        font_drawtext(x + 220, y, b, white, 2);
        y += 32;

        font_drawtext(x, y, "eflags     : 0x", white, 2);
        fmt_hex(r->eflags, b);
        font_drawtext(x + 220, y, b, white, 2);
        y += 48;

        font_drawtext(x, y, "System halted. Take a photo for debugging.",
                      white, 2);
        return;
    }

    vga_clear();
    vga_color(0x4F);                       /* 红底白字 */
    printf("*** YuanCore PANIC: exception %d (%s) ***\n\n",
           (int)r->int_no, names[r->int_no & 31]);
    printf("error code : 0x%X\n", r->err_code);
    printf("eip        : 0x%X\n", r->eip);
    printf("cs         : 0x%X\n", r->cs);
    printf("eflags     : 0x%X\n", r->eflags);
    printf("\nSystem halted.\n");
}

void kmain(unsigned int magic, unsigned int mbi_addr)
{
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)mbi_addr;
    int rc;

    cli();                                 /* 装好 IDT 之前绝不能开中断 */

    if (magic != MULTIBOOT_MAGIC) {
        vga_clear();
        vga_color(0x0F);
        printf("YuanCore: bad multiboot magic 0x%X\n", magic);
        for (;;)
            hlt();
    }

    gdt_init();
    tss_init();                            /* ring3 中断切内核栈 */
    idt_init();
    paging_init();                         /* 开分页：此后只碰已映射区域 */
    irq_init();                            /* 重映射 PIC，只放行 IRQ0/1/2/12 */

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
    fs_write("/apps/hello.app", app_hello_blob, APP_HELLO_LEN);

    kbd_init();
    mouse_init();                          /* PS/2 鼠标，IRQ12 */
    timer_init(100);                       /* 100Hz，顺带验证中断链路 */

    rc = fb_init(mbi);
    if (rc != 0) {
        sti();
        vga_clear();
        vga_color(0x0F);
        printf("YuanCore - i386 kernel\n\n");
        printf("Framebuffer unavailable, fb_init() = %d\n", rc);
        printf("  mbi flag : 0x%X\n", (unsigned int)mbi->flags);
        printf("  fb type  : %d, bpp %d\n",
               (int)mbi->framebuffer_type, (int)mbi->framebuffer_bpp);
        printf("\nFalling back to VGA text mode.\n");
        for (;;)
            hlt();
    }

    /* ---- 图形路径 ---- */
    theme_init();
    desktop_draw(mbi);

    syscall_init();                        /* 程序用的服务表(固定地址) */
    isr_register(0x80, syscall_dispatch);  /* 系统调用门已在 IDT 里开 DPL3 */

    console_init(SHELL_WIN_X + SHELL_PAD,
                 SHELL_WIN_Y + 36 + SHELL_PAD,      /* 36 = 标题栏高 */
                 SHELL_WIN_W - 2 * SHELL_PAD,
                 SHELL_WIN_H - 36 - 22 - 2 * SHELL_PAD);  /* 22 = 状态条高 */

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
