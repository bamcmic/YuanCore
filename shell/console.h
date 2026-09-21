/* shell/console.h - 图形模式下的滚动控制台
 *
 * 属于 shell/ 模块：颜色不在此处写死，一律从 mode/theme.c 读取，
 * 因此 settings 改色后调用 console_redraw() 即可整体换肤。
 */
#ifndef SHELL_CONSOLE_H
#define SHELL_CONSOLE_H

void console_init(int x, int y, int w, int h);

void console_putc(char c);
void console_puts(const char *s);
void console_clear(void);
void console_backspace(void);

/* 主题变化后调用：按当前主题重绘整块内容区 */
void console_redraw(void);

/* 只重画与 (x,y,w,h)（屏幕绝对坐标）相交的字符单元。
 * 供鼠标移开时"从内容源头刷新上一个位置"，替代可能过期的备份恢复。 */
void console_repaint_region(int x, int y, int w, int h);

/* --- 供行编辑使用 --- */
void console_cursor_get(int *col, int *row);
void console_cursor_set(int col, int row);
void console_clear_to_eol(void);
int  console_cols(void);
int  console_rows(void);

#endif /* SHELL_CONSOLE_H */
