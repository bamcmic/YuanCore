/* counter.c —— 由 YuanNode 节点式 IDE 生成
 * 目标：YuanCore ring3 用户程序（ELF64/x86-64，入口 app_main，int 0x80 系统调用）
 * 打包：编译后装进 .ycp 应用包（zip 仅 stored，见 api/ycp.md）再安装运行。
 * 不要手改本文件：改动请回到节点图，否则下次生成会被覆盖。
 */
#include "ycapi.h"

/* 变量 */
static int count;

/* 缓冲区 */
static char _cat1[160];
static char _itoa2[16];

/* 内核没有 libc，以下字符串函数由生成器注入 */
static void yn_itoa(int v, char *d)
{
    char t[16];
    int n = 0, i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { t[n++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    if (neg) d[i++] = '-';
    while (n > 0) d[i++] = t[--n];
    d[i] = 0;
}

static void yn_strcat(char *d, const char *s)
{
    while (*d) d++;
    while ((*d++ = *s++)) ;
}

static void yn_strcpy(char *d, const char *s)
{
    while ((*d++ = *s++)) ;
}

void app_main(void)
{
    yc_draw_window(320, 210, 400, 240, "Click Counter");
    while (1) {
        yc_button(344, 320, 160, 34, "Click me", 0);
        yn_strcpy(_cat1, "clicked: ");
        yn_itoa(count, _itoa2);
        yn_strcat(_cat1, _itoa2);
        yc_label(334, 380, _cat1);
        int _ev3, _key4 = -1, _dx5 = 0, _dy6 = 0, _btn7 = 0;
        _ev3 = yc_wait_event(&_key4, &_dx5, &_dy6, &_btn7);
        if ((_ev3 == 1)) {
            if ((_key4 == 27)) {
                yc_exit(0);
            } else {
                yc_refresh();
            }
        } else {
            int _mx8 = 0, _my9 = 0;
            yc_cursor(&_mx8, &_my9);
            if ((_mx8 >= 334 && _mx8 < 334 + 160 && _my9 >= 320 && _my9 < 320 + 34)) {
                if ((_btn7 != 0)) {
                    count += 1;
                }
            }
        }
    }
}
