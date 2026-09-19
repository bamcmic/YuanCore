/* ycapi.h - YuanCore 程序侧 API
 *
 * 程序通过 int 0x80 调用内核服务：eax=号，ebx/ecx/edx/esi/edi=前5个参数，
 * 第 6 个参数（仅 button 的 active）压在用户栈顶。
 * 下标一旦发布就不能变，只能在表尾追加。
 *
 * 构建（WSL，gcc）：
 *   gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -c hello.c -o hello.o
 *   ld  -m elf_i386 -e app_main -Ttext-segment=0x400000 -o hello.app hello.o
 * 或（zig）：
 *   zig cc -target x86-freestanding -ffreestanding -nostdlib -O2 \
 *       -Wl,--image-base=0x400000 -Wl,-e,app_main -o hello.app hello.c
 */
#ifndef YCAPI_H
#define YCAPI_H

enum {
    YC_PRINT = 0,     /* void yc_print(const char *)                        */
    YC_PUTC,          /* void yc_putc(char)                                 */
    YC_CLEAR,         /* void yc_clear(void)                                */
    YC_GETKEY,        /* int  yc_getkey(void)             阻塞              */
    YC_KEY_POLL,      /* int  yc_key_poll(void)           非阻塞，-1 无键    */
    YC_DRAW_WINDOW,   /* void yc_draw_window(x,y,w,h,title)                 */
    YC_LABEL,         /* void yc_label(x,y,text)                            */
    YC_BUTTON,        /* void yc_button(x,y,w,h,text,active) active=高亮    */
    YC_FILLRECT,      /* void yc_fillrect(x,y,w,h,0x00RRGGBB)               */
    YC_REFRESH,       /* void yc_refresh(void)  重绘桌面+终端               */
    YC_READ_FILE,     /* int  yc_read_file(path,buf,max)                    */
    YC_WRITE_FILE,    /* int  yc_write_file(path,data,len)                  */
    YC_MOUSE_POLL,    /* int  yc_mouse_poll(&dx,&dy,&btn)                   */
    YC_CURSOR,        /* void yc_cursor(&x,&y)        光标绝对位置           */
    YC_WAIT_EVENT,    /* int  yc_wait_event(&key,&dx,&dy,&btn) 1键 2鼠标    */
    YC_PRINT_DEC,     /* void yc_print_dec(int)                             */
    YC_EXIT = 18      /* void yc_exit(int code)         不返回              */
};

#define YC_KEY_ENTER 10
#define YC_KEY_ESC   27
#define YC_KEY_UP    0x100
#define YC_KEY_DOWN  0x101
#define YC_KEY_LEFT  0x102
#define YC_KEY_RIGHT 0x103

#define YC_BTN_LEFT 1

static inline int yc_syscall0(int n)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n));
    return r;
}

static inline int yc_syscall1(int n, int a)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a));
    return r;
}

static inline int yc_syscall2(int n, int a, int b)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b));
    return r;
}

static inline int yc_syscall3(int n, int a, int b, int c)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c));
    return r;
}

static inline int yc_syscall4(int n, int a, int b, int c, int d)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r)
                      : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d));
    return r;
}

static inline int yc_syscall5(int n, int a, int b, int c, int d, int e)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r)
                      : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e));
    return r;
}

/* 第 6 个参数走用户栈：push 后 int，内核从用户栈顶取 */
static inline void yc_button(int x, int y, int w, int h,
                             const char *text, int active)
{
    __asm__ volatile (
        "pushl %0\n"
        "int $0x80\n"
        "addl $4, %%esp"
        :
        : "m"(active), "a"(YC_BUTTON), "b"(x), "c"(y), "d"(w), "S"(h),
          "D"(text)
        : "memory");
}

static inline void yc_print(const char *s)      { yc_syscall1(YC_PRINT, (int)s); }
static inline void yc_putc(char c)              { yc_syscall1(YC_PUTC, c); }
static inline void yc_clear(void)               { yc_syscall0(YC_CLEAR); }
static inline int  yc_getkey(void)              { return yc_syscall0(YC_GETKEY); }
static inline int  yc_key_poll(void)            { return yc_syscall0(YC_KEY_POLL); }
static inline void yc_draw_window(int x, int y, int w, int h, const char *t)
{ yc_syscall5(YC_DRAW_WINDOW, x, y, w, h, (int)t); }
static inline void yc_label(int x, int y, const char *t)
{ yc_syscall3(YC_LABEL, x, y, (int)t); }
static inline void yc_fillrect(int x, int y, int w, int h, unsigned int c)
{ yc_syscall5(YC_FILLRECT, x, y, w, h, (int)c); }
static inline void yc_refresh(void)             { yc_syscall0(YC_REFRESH); }
static inline int  yc_read_file(const char *p, void *b, int m)
{ return yc_syscall3(YC_READ_FILE, (int)p, (int)b, m); }
static inline int  yc_write_file(const char *p, const void *d, int l)
{ return yc_syscall3(YC_WRITE_FILE, (int)p, (int)d, l); }
static inline int  yc_mouse_poll(int *dx, int *dy, int *btn)
{ return yc_syscall3(YC_MOUSE_POLL, (int)dx, (int)dy, (int)btn); }
static inline void yc_cursor(int *x, int *y)
{ yc_syscall2(YC_CURSOR, (int)x, (int)y); }
static inline int  yc_wait_event(int *key, int *dx, int *dy, int *btn)
{ return yc_syscall4(YC_WAIT_EVENT, (int)key, (int)dx, (int)dy, (int)btn); }
static inline void yc_print_dec(int v)          { yc_syscall1(YC_PRINT_DEC, v); }
static inline void yc_exit(int code)            { yc_syscall1(YC_EXIT, code); }

#endif /* YCAPI_H */
