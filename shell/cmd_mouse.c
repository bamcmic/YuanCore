/* shell/cmd_mouse.c - mouse 命令：实时显示鼠标事件
 *
 * 用它验证驱动：移动/点击鼠标会打印事件，Esc 或 Ctrl+C 退出。
 */
#include "shell.h"

#include "console.h"
#include "ui.h"

#include "../mode/mouse.h"
#include "../mode/io.h"
#include "../mode/kbd.h"

static void put_dec(int v)
{
    char t[12];
    int i = 0, j;

    if (v == 0) { console_putc('0'); return; }
    if (v < 0) { console_putc('-'); v = -v; }
    while (v > 0) { t[i++] = (char)('0' + (v % 10)); v /= 10; }
    for (j = 0; j < i; j++)
        console_putc(t[i - 1 - j]);
}

static void cmd_mouse(const char *args)
{
    int dx, dy, btn;
    long events = 0;

    (void)args;
    console_puts("mouse: move/click to see events, ESC to stop\n");
    ui_mouse_init();            /* 初始化光标位置 */

    for (;;) {
        int c = kbd_poll();

        if (c == KEY_ESC || c == 0x03)
            break;

        if (mouse_poll(&dx, &dy, &btn)) {
            ui_mouse(dx, dy, btn);
            events++;
            /* 只在按钮状态变化时打印，避免刷屏 */
            if (btn != 0 || (events % 24) == 0) {
                console_puts("  dx=");
                put_dec(dx);
                console_puts(" dy=");
                put_dec(dy);
                console_puts(" btn=");
                put_dec(btn);
                console_puts("\n");
            }
        }
        hlt();
    }
    console_puts("mouse: stopped\n");
}

static const struct shell_command mouse_cmd = {
    "mouse", "show live mouse events", cmd_mouse
};

void cmd_mouse_init(void)
{
    shell_register_command(&mouse_cmd);
}
