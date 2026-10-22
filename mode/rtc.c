/* rtc.c - CMOS 实时时钟
 *
 * 端口 0x70/0x71，标准 CMOS 布局：
 *   0x00 秒  0x02 分  0x04 时  0x07 日  0x08 月  0x09 年
 *   0x0A 状态A（bit7 = UIP 更新中）  0x0B 状态B（bit1=24h, bit2=二进制）
 *
 * 读取策略：等 UIP 清零后连读两组，一致才采纳，最多重试几次
 * —— 避免读到更新到一半的时间。
 *
 * rtc_write（NTP 同步）：沿用 CMOS 当前的格式位（BCD/二进制、
 * 12/24h），在 UIP=0 窗口写六个寄存器；写前 cli 保护，防止
 * RTC 中断/调度插进来打断连续写序列。
 */
#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define REG_SEC   0x00
#define REG_MIN   0x02
#define REG_HOUR  0x04
#define REG_DAY   0x07
#define REG_MON   0x08
#define REG_YEAR  0x09
#define REG_STA   0x0A
#define REG_STB   0x0B

static unsigned char cmos(unsigned char reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

/* UIP 置位期间 CMOS 正在更新，等它结束（带上限，别死等坏硬件） */
static int wait_uip(void)
{
    int t = 1000000;

    while (t-- > 0) {
        if ((cmos(REG_STA) & 0x80) == 0)
            return 0;
    }
    return -1;
}

static int bcd2bin(int v, int binary_mode)
{
    if (binary_mode)
        return v;
    return (v & 0x0F) + (v >> 4) * 10;
}

static int bin2bcd(int v, int binary_mode)
{
    if (binary_mode)
        return v;
    return ((v / 10) << 4) | (v % 10);
}

static void grab(struct rtc_time *t, int binary, int hour24)
{
    int h;

    t->sec  = bcd2bin(cmos(REG_SEC),  binary);
    t->min  = bcd2bin(cmos(REG_MIN),  binary);
    h       = cmos(REG_HOUR);
    t->day  = bcd2bin(cmos(REG_DAY),  binary);
    t->month= bcd2bin(cmos(REG_MON),  binary);
    t->year = bcd2bin(cmos(REG_YEAR), binary);

    h = bcd2bin(h & 0x7F, binary);
    if (!hour24 && (cmos(REG_HOUR) & 0x80) && h < 12)
        h += 12;                        /* 12 小时制的下午 */
    t->hour = h;
}

static int same(const struct rtc_time *a, const struct rtc_time *b)
{
    return a->sec == b->sec && a->min == b->min && a->hour == b->hour
        && a->day == b->day && a->month == b->month && a->year == b->year;
}

int rtc_read(struct rtc_time *out)
{
    struct rtc_time a, b;
    unsigned char stb;
    int binary, hour24, try;

    if (out == 0)
        return -1;

    for (try = 0; try < 4; try++) {
        if (wait_uip() != 0)
            return -1;

        stb    = cmos(REG_STB);
        binary = (stb & 0x04) != 0;
        hour24 = (stb & 0x02) != 0;

        grab(&a, binary, hour24);

        if (wait_uip() != 0)
            return -1;
        grab(&b, binary, hour24);

        if (same(&a, &b)) {
            *out = a;
            if (out->year < 100)
                out->year += 2000;      /* CMOS 只存两位年份 */
            return 0;
        }
    }
    return -1;
}

int rtc_write(const struct rtc_time *t)
{
    unsigned char stb;
    int binary, hour24;
    int yr;

    if (t == 0)
        return -1;
    if (t->sec < 0 || t->sec > 59 || t->min < 0 || t->min > 59
        || t->hour < 0 || t->hour > 23 || t->day < 1 || t->day > 31
        || t->month < 1 || t->month > 12
        || t->year < 2000 || t->year > 2099)
        return -1;                      /* 越界值直接拒绝,不写半截 */

    if (wait_uip() != 0)
        return -1;

    stb    = cmos(REG_STB);
    binary = (stb & 0x04) != 0;
    hour24 = (stb & 0x02) != 0;

    yr = t->year % 100;

    cli();                              /* 连续写序列不许被打断 */
    outb(CMOS_ADDR, REG_SEC);
    outb(CMOS_DATA, (unsigned char)bin2bcd(t->sec, binary));
    outb(CMOS_ADDR, REG_MIN);
    outb(CMOS_DATA, (unsigned char)bin2bcd(t->min, binary));
    outb(CMOS_ADDR, REG_HOUR);
    if (hour24) {                       /* QEMU 默认 24h;12h 制按规范带 PM 位 */
        outb(CMOS_DATA, (unsigned char)bin2bcd(t->hour, binary));
    } else {
        int h12 = t->hour % 12;

        if (h12 == 0)
            h12 = 12;
        outb(CMOS_DATA, (unsigned char)(bin2bcd(h12, binary)
              | (t->hour >= 12 ? 0x80 : 0)));
    }
    outb(CMOS_ADDR, REG_DAY);
    outb(CMOS_DATA, (unsigned char)bin2bcd(t->day, binary));
    outb(CMOS_ADDR, REG_MON);
    outb(CMOS_DATA, (unsigned char)bin2bcd(t->month, binary));
    outb(CMOS_ADDR, REG_YEAR);
    outb(CMOS_DATA, (unsigned char)bin2bcd(yr, binary));
    sti();

    return 0;
}
