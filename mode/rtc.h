/* rtc.h - CMOS 实时时钟（ polled，不用 IRQ8 ）
 *
 * 时钟中断（PIT 100Hz）早已在跑，这里补的是"真实时间"：
 * 从 CMOS 读墙钟时间，供任务栏时钟和 date 命令使用。
 */
#ifndef RTC_H
#define RTC_H

struct rtc_time {
    int sec, min, hour;
    int day, month, year;      /* year 为完整年份，如 2026 */
};

/* 读取当前时间。成功返回 0，硬件异常返回 -1（此时内容无意义）。
 * 内部对 UIP（更新中）做了等待和双读一致性校验。 */
int rtc_read(struct rtc_time *t);

#endif /* RTC_H */
