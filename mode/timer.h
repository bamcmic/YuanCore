/* timer.h - PIT 可编程间隔定时器 */
#ifndef TIMER_H
#define TIMER_H

void timer_init(unsigned int hz);          /* 注册 IRQ0 并启动 PIT */
unsigned int timer_ticks(void);            /* 上电以来的 tick 数 */
unsigned int timer_uptime_sec(void);       /* 开机秒数 */

#endif /* TIMER_H */
