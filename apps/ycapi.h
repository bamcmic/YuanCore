/* ycapi.h - YuanCore 程序侧 API（v2 / ABI 1.0）
 *
 * 架构自适应：__x86_64__ 时用 64 位传参(rax=号, rdi/rsi/rdx/r10/r8/r9
 * = 参数1..6)，i386 保持历史约定(eax=号, ebx/ecx/edx/esi/edi, 第 6 参
 * 压栈)。IDE 编译时换 target 即换 ABI，无需改源码。
 *
 * 契约文档：仓库根 API_SPEC.md。调用号只能追加不能改动。
 *
 * 构建（WSL）—— **必须带 -mno-sse 系**：QEMU 默认 CPU 没有 SSE2，
 * 用户态第一条 SSE 指令就是 #UD：
 *   i386:
 *     gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
 *         -mno-sse -mno-sse2 -mno-mmx -msoft-float -c hello.c -o hello.o
 *     ld  -m elf_i386 -e app_main -Ttext-segment=0x400000 -o hello.app
 *   x86-64:
 *     gcc -m64 -ffreestanding -fno-pie -fno-stack-protector -O2 \
 *         -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -msoft-float \
 *         -c hello.c -o hello.o
 *     ld  -m elf_x86_64 -e app_main -Ttext-segment=0x400000 -o hello.app
 */
#ifndef YCAPI_H
#define YCAPI_H

#define YC_ABI_MAJOR 1
#define YC_ABI_MINOR 0

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
    YC_EXIT = 18,     /* void yc_exit(int code)         不返回              */
    YC_CLOSE_ALL = 19,/* void yc_close_all(void)        关闭所有 UI 窗口    */
    YC_RANDOM = 20,   /* unsigned yc_random(void)       32位伪随机数        */
    YC_TICKS = 21,    /* unsigned yc_ticks(void)        PIT tick 数(100Hz)  */
    YC_SLEEP_TICKS = 22, /* void yc_sleep_ticks(n)      内核 hlt 等 n 个tick*/
    YC_PUTPIXEL = 23, /* void yc_putpixel(x,y,0x00RRGGBB)                    */
    YC_REG_COUNT = 24,/* int   yc_app_count(void)        应用注册表条目数     */
    YC_REG_NAME = 25, /* void  yc_app_name(idx,buf,max)  拷出应用名           */
    YC_REG_DESC = 26, /* void  yc_app_desc(idx,buf,max)  拷出描述             */
    YC_TERM_SHOW = 27,/* void  yc_term_show(on)          显隐 shell 终端窗口  */
    YC_DIR_LIST = 28, /* int   yc_dir_list(dir,idx,buf,max) 1=目录 0=文件 -1=完 */
    YC_DELETE = 29,   /* int   yc_delete(path)           递归删除             */
    YC_ABI = 30,      /* int   yc_abi(void)              (主<<8)|次           */

    /* ---- 网络(单连接阻塞 TCP,net/tcp.c) 追加于 ABI 1.1 ---- */
    YC_TCP_CONNECT = 31, /* int yc_tcp_connect(ip,port)  0成功,阻塞 */
    YC_TCP_LISTEN,       /* int yc_tcp_listen(port)      0成功,阻塞 */
    YC_TCP_SEND,         /* int yc_tcp_send(buf,len)     字节数/负   */
    YC_TCP_RECV,         /* int yc_tcp_recv(buf,max)     n/0=EOF/负  */
    YC_TCP_CLOSE = 35    /* void yc_tcp_close(void)                  */
};

#define YC_KEY_ENTER 10
#define YC_KEY_ESC   27
#define YC_KEY_UP    0x100
#define YC_KEY_DOWN  0x101
#define YC_KEY_LEFT  0x102
#define YC_KEY_RIGHT 0x103

#define YC_BTN_LEFT 1

#if defined(__x86_64__)
/* ---- x86-64 ABI：rax=号，rdi/rsi/rdx/r10/r8/r9 = 参数 1..6 ---- */
static inline long yc_sys(long n, long a1, long a2, long a3,
                          long a4, long a5, long a6)
{
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8v __asm__("r8")  = a5;
    register long r9v __asm__("r9")  = a6;

    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3),
                        "r"(r10), "r"(r8v), "r"(r9v)
                      : "memory");
    return r;
}

static inline void yc_print(const char *s)  { yc_sys(YC_PRINT, (long)s,0,0,0,0,0); }
static inline void yc_putc(char c)          { yc_sys(YC_PUTC, c,0,0,0,0,0); }
static inline void yc_clear(void)           { yc_sys(YC_CLEAR,0,0,0,0,0,0); }
static inline int  yc_getkey(void)          { return (int)yc_sys(YC_GETKEY,0,0,0,0,0,0); }
static inline int  yc_key_poll(void)        { return (int)yc_sys(YC_KEY_POLL,0,0,0,0,0,0); }
static inline void yc_draw_window(int x, int y, int w, int h, const char *t)
{ yc_sys(YC_DRAW_WINDOW, x, y, w, h, (long)t, 0); }
static inline void yc_label(int x, int y, const char *t)
{ yc_sys(YC_LABEL, x, y, (long)t, 0, 0, 0); }
static inline void yc_button(int x, int y, int w, int h,
                             const char *text, int active)
{ yc_sys(YC_BUTTON, x, y, w, h, (long)text, active); }
static inline void yc_fillrect(int x, int y, int w, int h, unsigned int c)
{ yc_sys(YC_FILLRECT, x, y, w, h, (long)c, 0); }
static inline void yc_refresh(void)         { yc_sys(YC_REFRESH,0,0,0,0,0,0); }
static inline int  yc_read_file(const char *p, void *b, int m)
{ return (int)yc_sys(YC_READ_FILE, (long)p, (long)b, m, 0, 0, 0); }
static inline int  yc_write_file(const char *p, const void *d, int l)
{ return (int)yc_sys(YC_WRITE_FILE, (long)p, (long)d, l, 0, 0, 0); }
static inline int  yc_mouse_poll(int *dx, int *dy, int *btn)
{ return (int)yc_sys(YC_MOUSE_POLL, (long)dx, (long)dy, (long)btn, 0, 0, 0); }
static inline void yc_cursor(int *x, int *y)
{ yc_sys(YC_CURSOR, (long)x, (long)y, 0, 0, 0, 0); }
static inline int  yc_wait_event(int *key, int *dx, int *dy, int *btn)
{ return (int)yc_sys(YC_WAIT_EVENT, (long)key, (long)dx, (long)dy, (long)btn, 0, 0); }
static inline void yc_print_dec(int v)      { yc_sys(YC_PRINT_DEC, v,0,0,0,0,0); }
static inline void yc_close_all(void)       { yc_sys(YC_CLOSE_ALL,0,0,0,0,0,0); }
static inline unsigned int yc_random(void)  { return (unsigned int)yc_sys(YC_RANDOM,0,0,0,0,0,0); }
static inline unsigned int yc_ticks(void)   { return (unsigned int)yc_sys(YC_TICKS,0,0,0,0,0,0); }
static inline void yc_sleep_ticks(unsigned int n) { yc_sys(YC_SLEEP_TICKS, n,0,0,0,0,0); }
static inline void yc_putpixel(int x, int y, unsigned int c)
{ yc_sys(YC_PUTPIXEL, x, y, (long)c, 0, 0, 0); }
static inline int  yc_app_count(void)       { return (int)yc_sys(YC_REG_COUNT,0,0,0,0,0,0); }
static inline void yc_app_name(int idx, char *buf, int max)
{ yc_sys(YC_REG_NAME, idx, (long)buf, max, 0, 0, 0); }
static inline void yc_app_desc(int idx, char *buf, int max)
{ yc_sys(YC_REG_DESC, idx, (long)buf, max, 0, 0, 0); }
static inline void yc_term_show(int on)     { yc_sys(YC_TERM_SHOW, on,0,0,0,0,0); }
static inline int  yc_dir_list(const char *dir, int idx, char *buf, int max)
{ return (int)yc_sys(YC_DIR_LIST, (long)dir, idx, (long)buf, max, 0, 0); }
static inline int  yc_delete(const char *path)
{ return (int)yc_sys(YC_DELETE, (long)path, 0, 0, 0, 0, 0); }
static inline int  yc_abi(void)             { return (int)yc_sys(YC_ABI,0,0,0,0,0,0); }
static inline void yc_exit(int code)        { yc_sys(YC_EXIT, code,0,0,0,0,0); }

#else
/* ---- i386 ABI（历史约定，勿改动）---- */

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
static inline void yc_close_all(void)           { yc_syscall0(YC_CLOSE_ALL); }
static inline unsigned int yc_random(void)      { return (unsigned int)yc_syscall0(YC_RANDOM); }
static inline unsigned int yc_ticks(void)       { return (unsigned int)yc_syscall0(YC_TICKS); }
static inline void yc_sleep_ticks(unsigned int n) { yc_syscall1(YC_SLEEP_TICKS, (int)n); }
static inline void yc_putpixel(int x, int y, unsigned int c)
{ yc_syscall3(YC_PUTPIXEL, x, y, (int)c); }
static inline int  yc_app_count(void)           { return yc_syscall0(YC_REG_COUNT); }
static inline void yc_app_name(int idx, char *buf, int max)
{ yc_syscall3(YC_REG_NAME, idx, (int)buf, max); }
static inline void yc_app_desc(int idx, char *buf, int max)
{ yc_syscall3(YC_REG_DESC, idx, (int)buf, max); }
static inline void yc_term_show(int on)         { yc_syscall1(YC_TERM_SHOW, on); }
static inline int  yc_dir_list(const char *dir, int idx, char *buf, int max)
{ return yc_syscall4(YC_DIR_LIST, (int)dir, idx, (int)buf, max); }
static inline int  yc_delete(const char *path)
{ return yc_syscall1(YC_DELETE, (int)path); }
static inline int  yc_abi(void)                 { return yc_syscall0(YC_ABI); }
static inline void yc_exit(int code)            { yc_syscall1(YC_EXIT, code); }

/* ---- 网络(单连接阻塞 TCP) ---- */
static inline int  yc_tcp_connect(unsigned int ip, unsigned short port)
{ return yc_syscall2(YC_TCP_CONNECT, (int)ip, (int)port); }
static inline int  yc_tcp_listen(unsigned short port)
{ return yc_syscall1(YC_TCP_LISTEN, (int)port); }
static inline int  yc_tcp_send(const void *buf, int len)
{ return yc_syscall2(YC_TCP_SEND, (int)buf, len); }
static inline int  yc_tcp_recv(void *buf, int max)
{ return yc_syscall2(YC_TCP_RECV, (int)buf, max); }
static inline void yc_tcp_close(void)           { yc_syscall0(YC_TCP_CLOSE); }

#endif /* __x86_64__ */

#endif /* YCAPI_H */
