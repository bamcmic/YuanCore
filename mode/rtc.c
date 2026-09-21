/* rtc.c - CMOS 实时时钟
 *
 * 端口 0x70/0x71，标准 CMOS 布局：
 *   0x00 秒  0x02 分  0x04 时  0x07 日  0x08 月  0x09 年
 *   0x0A 状态A（bit7 = UIP 更新中）  0x0B 状态B（bit1=24h, bit2=二进制）
 *
 * 读取策略：等 UIP 清零后连读两组，一致才采纳，最多重试几次
 * —— 避免读到更新到一半的时间。
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
