/* kbd.h - PS/2 键盘驱动
 *
 * 可打印字符返回 ASCII(0-127)。
 * 方向键/Delete/Home/End 等 0xE0 扩展键返回 0x100 以上的 KEY_* 常量。
 * Ctrl+字母 返回标准控制码(如 Ctrl+C=0x03, Ctrl+L=0x0C, Ctrl+U=0x15)。
 */
#ifndef KBD_H
#define KBD_H

/* 扩展键（不会与 ASCII 冲突） */
#define KEY_UP      0x100
#define KEY_DOWN    0x101
#define KEY_LEFT    0x102
#define KEY_RIGHT   0x103
#define KEY_DELETE  0x104
#define KEY_HOME    0x105
#define KEY_END     0x106
#define KEY_PGUP    0x107
#define KEY_PGDOWN  0x108
#define KEY_WIN     0x109   /* 左/右 Win 键（唤起/收起任务栏） */

/* 常用控制字符（ASCII 本身就有） */
#define KEY_TAB        9
#define KEY_ENTER     10
#define KEY_BACKSPACE  8
#define KEY_ESC       27

void kbd_init(void);

/* 阻塞读取：没按键时 hlt 等中断。不可识别的键返回 0。 */
int kbd_getchar(void);

/* 非阻塞：有就返回键值，没有返回 -1 */
int kbd_poll(void);

#endif /* KBD_H */
