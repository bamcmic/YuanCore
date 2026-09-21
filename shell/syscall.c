/* shell/syscall.c - 程序系统调用
 *
 * 两部分：
 *  1) syscall_init()    把服务函数指针写到固定地址(MEM_SYSCALL_BASE)，
 *     程序侧 apps/ycapi.h 按约定下标取用。
 *  2) syscall_dispatch() 处理 int 0x80：eax=号，ebx/ecx/edx/esi/edi=前5个参数，
 *     第 6 个参数放在用户栈顶（*(unsigned int*)r->user_esp，push 后 int）。
 *     返回值写回 eax。YC_EXIT 直接切回内核现场（user_exit，不返回）。
 *
 * 下标一旦发布就不能变，只能在表尾追加。
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
#include "registry.h"

#define YC_MAX_SYSCALLS 32

static void *table[YC_MAX_SYSCALLS];

/* 把内核侧字符串拷进用户缓冲（ring3 读不了内核段，不能直接给指针） */
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

    /* 下标 24-27 见 dispatch：注册表与终端显隐（参数组合多样，走分支） */

    rand_state = timer_ticks() | 1;

    for (i = 0; i < YC_MAX_SYSCALLS; i++)
        t[i] = table[i];
}

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
    case 19: ui_close_all(); r->eax = 0; break;   /* YC_CLOSE_ALL */
    case 20: r->eax = rand_next(); break;         /* YC_RANDOM */
    case 21: r->eax = timer_ticks(); break;       /* YC_TICKS */
    case 22: sleep_ticks((unsigned int)a); r->eax = 0; break;  /* SLEEP */
    case 23: fb_putpixel(a, b, (unsigned int)c); r->eax = 0; break;
    /* 24 YC_REG_COUNT：应用数 */
    case 24: r->eax = (unsigned int)registry_count(); break;
    /* 25 YC_REG_NAME(idx, buf, max)：拷出应用名 */
    case 25: copy_out((char *)b, c, registry_name(a));
             r->eax = 0; break;
    /* 26 YC_REG_DESC(idx, buf, max)：拷出描述 */
    case 26: copy_out((char *)b, c, registry_desc(a));
             r->eax = 0; break;
    /* 27 YC_TERM_SHOW(on)：显隐 YuanCore Shell 窗口 */
    case 27: desktop_terminal_show(a);
             ui_refresh();
             r->eax = 0; break;
    case 18:                                     /* YC_EXIT */
        exec_set_exit((int)a);
        user_exit();                             /* 不返回 */
        break;
    default:
        r->eax = (unsigned int)-1;
        break;
    }
}
