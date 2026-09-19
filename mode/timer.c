/* timer.c - PIT(channel 0) 时钟
 *
 * 输入时钟 1193182 Hz，除以 divisor 得到中断频率。
 * 只做计时（uptime），也顺便验证中断链路是通的。
 */
#include "timer.h"
#include "io.h"
#include "irq.h"

#define PIT_CMD   0x43
#define PIT_DATA0 0x40
#define PIT_FREQ  1193182U

static volatile unsigned int ticks;
static unsigned int hz_setting = 100;

static void timer_callback(struct registers *r)
{
    (void)r;
    ticks++;
}

void timer_init(unsigned int hz)
{
    unsigned int divisor;

    if (hz == 0)
        hz = 100;
    hz_setting = hz;

    divisor = PIT_FREQ / hz;
    if (divisor == 0)
        divisor = 1;

    irq_register(0, timer_callback);

    outb(PIT_CMD, 0x36);                       /* channel0, lobyte/hibyte, 方波 */
    outb(PIT_DATA0, (unsigned char)(divisor & 0xFF));
    outb(PIT_DATA0, (unsigned char)((divisor >> 8) & 0xFF));
}

unsigned int timer_ticks(void)
{
    return ticks;
}

unsigned int timer_uptime_sec(void)
{
    return ticks / hz_setting;
}
