/* kbd.c - PS/2 键盘(IRQ1, 端口 0x60/0x64)
 *
 * Scan Code Set 1：
 *   - 普通键 make/break(0x80)
 *   - 0xE0 前缀的扩展键（方向键 / Delete / Home / End / PgUp / PgDn）
 *   - Shift(0x2A/0x36)、Ctrl(0x1D) 状态跟踪
 * Ctrl+字母 转成标准控制码，shell 侧就能用 0x03/0x0C/0x15 识别快捷键。
 */
#include "kbd.h"
#include "io.h"
#include "irq.h"
#include "registers.h"

#define KBD_DATA 0x60
#define KBD_STAT 0x64

#define KB_BUF 64

static volatile unsigned char buf[KB_BUF];
static volatile unsigned int  head, tail;

static int shift_down;
static int ctrl_down;
static int ext_pending;                 /* 上一个是 0xE0，本字节是扩展码 */

/* Scan Code Set 1, 不按 Shift */
static const char map_lo[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,          /* 59-68 F1-F10 */
    0,0,0,0,0,                     /* 69-73 */
    0,0,0,0,0,0,0,0,0,0            /* 74-83 */
};

static const char map_hi[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0
};

static void buf_push(int c)
{
    unsigned int next = (head + 1) % KB_BUF;

    if (next == tail)
        return;                     /* 缓冲满，丢键 */
    buf[head] = (unsigned char)c;
    head = next;
}

static int buf_pop(void)
{
    unsigned char c;

    if (tail == head)
        return -1;
    c = buf[tail];
    tail = (tail + 1) % KB_BUF;
    return c;
}

static int ctrl_code(char c)
{
    /* Ctrl+字母 → 控制码 1..26 */
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 1;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 1;
    return 0;
}

static void handle_extended(unsigned char code)
{
    switch (code) {
    case 0x48: buf_push(KEY_UP);      break;
    case 0x50: buf_push(KEY_DOWN);    break;
    case 0x4B: buf_push(KEY_LEFT);    break;
    case 0x4D: buf_push(KEY_RIGHT);   break;
    case 0x53: buf_push(KEY_DELETE);  break;
    case 0x47: buf_push(KEY_HOME);    break;
    case 0x4F: buf_push(KEY_END);     break;
    case 0x49: buf_push(KEY_PGUP);    break;
    case 0x51: buf_push(KEY_PGDOWN);  break;
    default:   break;                 /* 其余扩展键忽略 */
    }
}

static void kbd_callback(struct registers *r)
{
    unsigned char sc;
    unsigned char code;

    (void)r;

    sc = inb(KBD_DATA);

    if (sc == 0xE0) {
        ext_pending = 1;
        return;
    }

    if (ext_pending != 0) {
        ext_pending = 0;
        if (sc & 0x80)
            return;                     /* 扩展键 break，忽略 */
        handle_extended(sc);
        return;
    }

    if (sc & 0x80) {                    /* break code：松键 */
        code = (unsigned char)(sc & 0x7F);
        if (code == 0x2A || code == 0x36)
            shift_down = 0;
        if (code == 0x1D)
            ctrl_down = 0;
        return;
    }

    code = sc;
    if (code == 0x2A || code == 0x36) {   /* 左/右 Shift */
        shift_down = 1;
        return;
    }
    if (code == 0x1D) {                   /* 左 Ctrl */
        ctrl_down = 1;
        return;
    }
    if (code >= 128)
        return;

    {
        char c = shift_down ? map_hi[code] : map_lo[code];
        if (c == 0)
            return;
        if (ctrl_down != 0) {
            int k = ctrl_code(c);
            if (k != 0)
                buf_push(k);
        } else {
            buf_push(c);
        }
    }
}

void kbd_init(void)
{
    head = tail = 0;
    shift_down = 0;
    ctrl_down = 0;
    ext_pending = 0;
    irq_register(1, kbd_callback);
    /* IRQ1 在 irq_init 里已放行 */
}

int kbd_poll(void)
{
    return buf_pop();
}

int kbd_getchar(void)
{
    for (;;) {
        int c = buf_pop();
        if (c >= 0)
            return c;
        hlt();                      /* 没键就睡，等 IRQ 唤醒 */
    }
}
