/* shell/syscall.c - 程序系统调用（双架构）
 *
 * 两部分：
 *  1) syscall_init()    把服务函数指针写到固定地址(MEM_SYSCALL_BASE)。
 *  2) int 0x80 分发:
 *     i386: syscall_dispatch(struct registers*)  —— ebx/ecx/edx/esi/edi,
 *           第 6 参在用户栈顶
 *     x64:  syscall64_dispatch(struct irq_frame64*) —— rdi/rsi/rdx/r10/
 *           r8/r9 全 64 位传参(rax=调用号), YC_EXIT 走 user_exit64
 *
 * 调用号契约见 apps/ycapi.h 与 API_SPEC.md,只能追加不能改动。
 */
#include "syscall.h"

#include "console.h"
#include "ui.h"

#include "../mode/kbd.h"
#include "../mode/mouse.h"
#include "../mode/fs.h"
#include "../mode/fb.h"
#include "../mode/timer.h"
#include "../mode/io.h"
#include "../mode/memmap.h"
#include "../mode/exec.h"
#include "../mode/usermode.h"
#include "../mode/desktop.h"
#include "../net/tcp.h"
#include "registry.h"

#include "../apps/ycapi.h"   /* YC_ABI_MAJOR/MINOR:单一真源 */

#include "../apps/ycapi.h"   /* YC_ABI_MAJOR/MINOR:单一真源 */

#ifdef YC_X64
#include "../x64/idt64.h"
#include "../x64/registers64.h"
#endif

#define YC_MAX_SYSCALLS 64
#define YC_ABI_VALUE    ((YC_ABI_MAJOR << 8) | YC_ABI_MINOR)

static void *table[YC_MAX_SYSCALLS];

#ifdef YC_X64
void syscall64_dispatch(void *framep);   /* 前向:syscall_init 里登记门 */
#endif

/* 把内核侧字符串拷进用户缓冲(不能直接给内核指针) */
static void copy_out(char *dst, int max, const char *src)
{
    int i = 0;

    if (dst == 0 || max <= 0)
        return;
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != 0 && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* xorshift32：种子取自开机 tick，供程序用 yc_random() 取随机数 */
static unsigned int rand_state;

static unsigned int rand_next(void)
{
    rand_state ^= rand_state << 13;
    rand_state ^= rand_state >> 17;
    rand_state ^= rand_state << 5;
    return rand_state;
}

static void sleep_ticks(unsigned int n)
{
    unsigned int t0 = timer_ticks();

    while (timer_ticks() - t0 < n)
        hlt();                      /* 内核侧睡眠，CPU 不空转 */
}

void syscall_init(void)
{
    volatile void **t = (volatile void **)MEM_SYSCALL_BASE;
    int i;

    for (i = 0; i < YC_MAX_SYSCALLS; i++)
        t[i] = 0;

    /* 下标与 apps/ycapi.h 的枚举严格一致，勿改动已有项 */
    table[0]  = (void *)console_puts;                /* YC_PRINT       */
    table[1]  = (void *)console_putc;                /* YC_PUTC        */
    table[2]  = (void *)console_clear;               /* YC_CLEAR       */
    table[3]  = (void *)kbd_getchar;                 /* YC_GETKEY      */
    table[4]  = (void *)kbd_poll;                    /* YC_KEY_POLL    */
    table[5]  = (void *)desktop_draw_window;         /* YC_DRAW_WINDOW */
    table[6]  = (void *)ui_label;                    /* YC_LABEL       */
    table[7]  = (void *)ui_draw_button;              /* YC_BUTTON      */
    table[8]  = (void *)fb_fillrect;                 /* YC_FILLRECT    */
    table[9]  = (void *)ui_refresh;                  /* YC_REFRESH     */
    table[10] = (void *)fs_read;                     /* YC_READ_FILE   */
    table[11] = (void *)fs_write;                    /* YC_WRITE_FILE  */
    table[12] = (void *)mouse_poll;                  /* YC_MOUSE_POLL  */
    table[13] = (void *)ui_cursor_get;               /* YC_CURSOR      */
    table[14] = (void *)ui_wait_event;               /* YC_WAIT_EVENT  */
    table[15] = (void *)ui_print_dec;                /* YC_PRINT_DEC   */
    table[19] = (void *)ui_close_all;                /* YC_CLOSE_ALL   */
    table[20] = (void *)rand_next;                   /* YC_RANDOM      */
    table[21] = (void *)timer_ticks;                 /* YC_TICKS       */
    table[22] = (void *)sleep_ticks;                 /* YC_SLEEP_TICKS */
    table[23] = (void *)fb_putpixel;                 /* YC_PUTPIXEL    */
    table[31] = (void *)tcp_connect;                 /* YC_TCP_CONNECT */
    table[32] = (void *)tcp_listen;                  /* YC_TCP_LISTEN  */
    table[33] = (void *)tcp_send;                    /* YC_TCP_SEND    */
    table[34] = (void *)tcp_recv;                    /* YC_TCP_RECV    */
    table[35] = (void *)tcp_close;                   /* YC_TCP_CLOSE   */

    rand_state = timer_ticks() | 1;

    for (i = 0; i < YC_MAX_SYSCALLS; i++)
        t[i] = table[i];

#ifdef YC_X64
    /* int 0x80 门:DPL3 + 绑定 64 位分发 */
    syscall64_register_gate(syscall64_dispatch);
#endif
}

/* ---- 公共服务实现(两架构 dispatch 复用) ---- */

static unsigned int svc_random(void) { return rand_next(); }

static unsigned int svc_ticks(void) { return timer_ticks(); }

static void svc_sleep(unsigned int n) { sleep_ticks(n); }

/* YC_DIR_LIST: 返回 1=目录 0=文件 -1=无更多,名字拷入 buf */
static long svc_dir_list(long a, long b, long c, long d)
{
    int is_dir = 0;
    const char *nm = fs_list((const char *)(unsigned long)a,
                             (int)b, &is_dir, 0);

    if (nm == 0)
        return -1;
    copy_out((char *)(unsigned long)c, (int)d, nm);
    return is_dir ? 1 : 0;
}

#ifdef YC_X64

/* ---- x86-64 分发:rax=号, rdi/rsi/rdx/r10/r8/r9 = 参数 1..6 ---- */
void syscall64_dispatch(void *framep)
{
    struct irq_frame64 *r = (struct irq_frame64 *)framep;
    unsigned int n = (unsigned int)r->rax;
    long a = (long)r->rdi;
    long b = (long)r->rsi;
    long c = (long)r->rdx;
    long d = (long)r->r10;
    long e = (long)r->r8;
    long f = (long)r->r9;

    (void)e; (void)f;

    switch (n) {
    case 0:  console_puts((const char *)(unsigned long)a); r->rax = 0; break;
    case 1:  console_putc((char)a); r->rax = 0; break;
    case 2:  console_clear(); r->rax = 0; break;
    case 3:  r->rax = (unsigned long)(int)kbd_getchar(); break;
    case 4:  r->rax = (unsigned long)(int)kbd_poll(); break;
    case 5:  desktop_draw_window((int)a, (int)b, (int)c, (int)d,
                                 (const char *)(unsigned long)e);
             r->rax = 0; break;
    case 6:  ui_label((int)a, (int)b, (const char *)(unsigned long)c);
             r->rax = 0; break;
    case 7:  ui_draw_button((int)a, (int)b, (int)c, (int)d,
                            (const char *)(unsigned long)e, (int)f);
             r->rax = 0; break;
    case 8:  fb_fillrect((int)a, (int)b, (int)c, (int)d,
                         (unsigned int)e);
             r->rax = 0; break;
    case 9:  ui_refresh(); r->rax = 0; break;
    case 10: r->rax = (unsigned long)(int)fs_read((const char *)(unsigned long)a,
                                                  (void *)(unsigned long)b,
                                                  (int)c);
             break;
    case 11: r->rax = (unsigned long)(int)fs_write((const char *)(unsigned long)a,
                                                   (const void *)(unsigned long)b,
                                                   (int)c);
             break;
    case 12: r->rax = (unsigned long)(int)mouse_poll((int *)(unsigned long)a,
                                                     (int *)(unsigned long)b,
                                                     (int *)(unsigned long)c);
             break;
    case 13: ui_cursor_get((int *)(unsigned long)a, (int *)(unsigned long)b);
             r->rax = 0; break;
    case 14: r->rax = (unsigned long)(int)ui_wait_event(
                         (int *)(unsigned long)a, (int *)(unsigned long)b,
                         (int *)(unsigned long)c, (int *)(unsigned long)d);
             break;
    case 15: ui_print_dec((int)a); r->rax = 0; break;
    case 18:                                      /* YC_EXIT */
             exec_set_exit((int)a);
             user_exit64();                       /* 不返回 */
             break;
    case 19: ui_close_all(); r->rax = 0; break;
    case 20: r->rax = svc_random(); break;
    case 21: r->rax = svc_ticks(); break;
    case 22: svc_sleep((unsigned int)a); r->rax = 0; break;
    case 23: fb_putpixel((int)a, (int)b, (unsigned int)c); r->rax = 0; break;
    case 24: r->rax = (unsigned long)registry_count(); break;
    case 25: copy_out((char *)(unsigned long)b, (int)c,
                      registry_name((int)a));
             r->rax = 0; break;
    case 26: copy_out((char *)(unsigned long)b, (int)c,
                      registry_desc((int)a));
             r->rax = 0; break;
    case 27: desktop_terminal_show((int)a);
             ui_refresh();
             r->rax = 0; break;
    case 28: r->rax = (unsigned long)svc_dir_list(a, b, c, d); break;
    case 29: r->rax = (unsigned long)(int)fs_rmtree(
                         (const char *)(unsigned long)a);
             break;
    case 30: r->rax = YC_ABI_VALUE; break;
    case 31: r->rax = (unsigned long)(int)tcp_connect(
                 (unsigned int)a, (unsigned short)b); break;
    case 32: r->rax = (unsigned long)(int)tcp_listen(
                 (unsigned short)a); break;
    case 33: r->rax = (unsigned long)(int)tcp_send(
                 (const unsigned char *)(unsigned long)a,
                 (unsigned int)b); break;
    case 34: r->rax = (unsigned long)(int)tcp_recv(
                 (unsigned char *)(unsigned long)a,
                 (unsigned int)b); break;
    case 35: tcp_close(); r->rax = 0; break;
    default:
             r->rax = (unsigned long)-1;
             break;
    }
}

#else /* !YC_X64 */

/* ---- i386 分发(eax=号, ebx/ecx/edx/esi/edi, 第 6 参用户栈顶) ---- */
void syscall_dispatch(struct registers *r)
{
    unsigned int n = r->eax;
    int a = (int)r->ebx;
    int b = (int)r->ecx;
    int c = (int)r->edx;
    int d = (int)r->esi;
    int e = (int)r->edi;
    int f = (r->user_esp != 0) ? *(int *)(unsigned long)r->user_esp : 0;

    switch (n) {
    case 0:  console_puts((const char *)a); r->eax = 0; break;
    case 1:  console_putc((char)a); r->eax = 0; break;
    case 2:  console_clear(); r->eax = 0; break;
    case 3:  r->eax = (unsigned int)kbd_getchar(); break;
    case 4:  r->eax = (unsigned int)kbd_poll(); break;
    case 5:  desktop_draw_window(a, b, c, d, (const char *)e);
             r->eax = 0; break;
    case 6:  ui_label(a, b, (const char *)c); r->eax = 0; break;
    case 7:  ui_draw_button(a, b, c, d, (const char *)e, f);
             r->eax = 0; break;
    case 8:  fb_fillrect(a, b, c, d, (unsigned int)e); r->eax = 0; break;
    case 9:  ui_refresh(); r->eax = 0; break;
    case 10: r->eax = (unsigned int)fs_read((const char *)a, (void *)b, c);
             break;
    case 11: r->eax = (unsigned int)fs_write((const char *)a,
                                             (const void *)b, c);
             break;
    case 12: r->eax = (unsigned int)mouse_poll((int *)a, (int *)b, (int *)c);
             break;
    case 13: ui_cursor_get((int *)a, (int *)b); r->eax = 0; break;
    case 14: r->eax = (unsigned int)ui_wait_event((int *)a, (int *)b,
                                                  (int *)c, (int *)d);
             break;
    case 15: ui_print_dec(a); r->eax = 0; break;
    case 18:                                      /* YC_EXIT */
             exec_set_exit(a);
             user_exit();                         /* 不返回 */
             break;
    case 19: ui_close_all(); r->eax = 0; break;
    case 20: r->eax = svc_random(); break;
    case 21: r->eax = svc_ticks(); break;
    case 22: svc_sleep((unsigned int)a); r->eax = 0; break;
    case 23: fb_putpixel(a, b, (unsigned int)c); r->eax = 0; break;
    case 24: r->eax = (unsigned int)registry_count(); break;
    case 25: copy_out((char *)b, c, registry_name(a));
             r->eax = 0; break;
    case 26: copy_out((char *)b, c, registry_desc(a));
             r->eax = 0; break;
    case 27: desktop_terminal_show(a);
             ui_refresh();
             r->eax = 0; break;
    case 28: r->eax = (unsigned int)svc_dir_list(a, b, c, d); break;
    case 29: r->eax = (unsigned int)fs_rmtree((const char *)a); break;
    case 30: r->eax = YC_ABI_VALUE; break;
    default:
             r->eax = (unsigned int)-1;
             break;
    }
}

#endif /* YC_X64 */
