/* mouse.c - PS/2 鼠标(IRQ12, 走 8042 的 aux 口)
 *
 * 数据包 3 字节：[0]=按键+符号位, [1]=dx, [2]=dy。
 * [0] 的 bit3 恒为 1，靠它做包同步。
 * 初始化顺序：开 aux 口 → 改控制器配置(开 IRQ12、放行鼠标时钟) →
 * 设默认值(0xF6) → 开数据上报(0xF4)。
 */
#include "mouse.h"
#include "io.h"
#include "irq.h"
#include "registers.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64

#define EVT_BUF 32

struct mouse_evt {
    int dx, dy, btn;
};

static volatile struct mouse_evt evts[EVT_BUF];
static volatile unsigned int head, tail;

static unsigned char pkt[3];
static int pkt_idx;

static void wait_write(void)
{
    int t = 100000;

    while (t-- > 0 && (inb(PS2_STAT) & 0x02))
        ;
}

static void wait_read(void)
{
    int t = 100000;

    while (t-- > 0 && !(inb(PS2_STAT) & 0x01))
        ;
}

static void cmd(unsigned char b)
{
    wait_write();
    outb(PS2_STAT, b);
}

static void push(int dx, int dy, int btn)
{
    unsigned int next = (head + 1) % EVT_BUF;

    if (next == tail)
        return;
    evts[head].dx  = dx;
    evts[head].dy  = dy;
    evts[head].btn = btn;
    head = next;
}

static void mouse_callback(struct registers *r)
{
    unsigned char d = inb(PS2_DATA);
    int dx, dy;

    (void)r;

    /* 包同步：第一字节的 bit3 必须是 1 */
    if (pkt_idx == 0 && (d & 0x08) == 0)
        return;

    pkt[pkt_idx++] = d;
    if (pkt_idx < 3)
        return;
    pkt_idx = 0;

    dx = (int)pkt[1];
    dy = (int)pkt[2];
    if (pkt[0] & 0x10) dx -= 256;       /* x 负溢出 */
    if (pkt[0] & 0x20) dy -= 256;       /* y 负溢出 */

    /* 屏幕坐标系 y 向下，与硬件相反 */
    push(dx, -dy, pkt[0] & 0x07);
}

void mouse_init(void)
{
    unsigned char cfg;

    head = tail = 0;
    pkt_idx = 0;

    irq_register(12, mouse_callback);

    cmd(0xA8);                          /* 启用 aux 口 */
    cmd(0x20);                          /* 读控制器配置 */
    wait_read();
    cfg = inb(PS2_DATA);
    cfg |= 0x02;                        /* 打开 IRQ12 */
    cfg &= (unsigned char)~0x20;        /* 放行鼠标时钟 */
    cmd(0x60);                          /* 写回配置 */
    wait_write();
    outb(PS2_DATA, cfg);

    cmd(0xD4);                          /* 下一字节发给鼠标 */
    wait_write();
    outb(PS2_DATA, 0xF6);               /* 默认设置 */

    cmd(0xD4);
    wait_write();
    outb(PS2_DATA, 0xF4);               /* 开始数据上报 */

    pic_unmask(12);
}

int mouse_poll(int *dx, int *dy, int *btn)
{
    unsigned int i = tail;

    if (tail == head)
        return 0;
    tail = (tail + 1) % EVT_BUF;
    if (dx)  *dx  = evts[i].dx;
    if (dy)  *dy  = evts[i].dy;
    if (btn) *btn = evts[i].btn;
    return 1;
}
