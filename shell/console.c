/* shell/console.c - 图形模式滚动控制台
 *
 * 图形模式没有硬件文本层，自己维护字符缓冲，滚动时整块重绘。
 * 字符单元 8x18（字模 8x16 + 2px 行距）。
 *
 * 颜色一律走 mode/theme.c，所以 settings 改色后 console_redraw()
 * 就能让历史输出一起换肤。
 */
#include "console.h"

#include "../mode/fb.h"
#include "../mode/font.h"
#include "../mode/theme.h"

#define CELL_W 8
#define CELL_H 18

#define CON_MAX_COLS 100
#define CON_MAX_ROWS 60

static int cx, cy;                          /* 区域在屏幕上的位置与大小 */
static int cols, rows;

static char cell_ch[CON_MAX_ROWS][CON_MAX_COLS];

static int cur_col, cur_row;

void console_init(int x, int y, int w, int h)
{
    cx = x;
    cy = y;

    cols = w / CELL_W;
    rows = h / CELL_H;
    if (cols > CON_MAX_COLS) cols = CON_MAX_COLS;
    if (rows > CON_MAX_ROWS) rows = CON_MAX_ROWS;

    cur_col = cur_row = 0;
    console_clear();
}

static void redraw(void)
{
    unsigned int fg = theme_get(THEME_CONSOLE_FG);
    unsigned int bg = theme_get(THEME_CONSOLE_BG);
    int r, c;

    fb_fillrect(cx, cy, cols * CELL_W, rows * CELL_H, bg);
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            char ch2 = cell_ch[r][c];
            if (ch2 == 0 || ch2 == ' ')
                continue;
            font_drawchar(cx + c * CELL_W, cy + r * CELL_H, ch2, fg, 1);
        }
    }
}

void console_redraw(void)
{
    redraw();
}

static void scroll_up(void)
{
    int r, c;

    for (r = 0; r < rows - 1; r++) {
        for (c = 0; c < cols; c++)
            cell_ch[r][c] = cell_ch[r + 1][c];
    }
    for (c = 0; c < cols; c++)
        cell_ch[rows - 1][c] = 0;
    redraw();
}

void console_clear(void)
{
    int r, c;

    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            cell_ch[r][c] = 0;
    cur_col = cur_row = 0;
    redraw();
}

void console_putc(char c)
{
    unsigned int fg = theme_get(THEME_CONSOLE_FG);
    unsigned int bg = theme_get(THEME_CONSOLE_BG);
    int i;

    switch (c) {
    case '\n':
        cur_col = 0;
        cur_row++;
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\t':
        do {
            console_putc(' ');
        } while (cur_col % 8 != 0);
        return;
    case '\b':
        if (cur_col > 0) {
            cur_col--;
            cell_ch[cur_row][cur_col] = 0;
            font_drawchar(cx + cur_col * CELL_W, cy + cur_row * CELL_H,
                          ' ', bg, 1);
        }
        return;
    default:
        if (cur_col >= cols) {
            cur_col = 0;
            cur_row++;
        }
        if (cur_row >= rows) {
            scroll_up();
            cur_row = rows - 1;
        }
        cell_ch[cur_row][cur_col] = c;
        font_drawchar(cx + cur_col * CELL_W, cy + cur_row * CELL_H,
                      c, fg, 1);
        cur_col++;
        return;
    }

    if (cur_row >= rows) {
        scroll_up();
        cur_row = rows - 1;
    }
    for (i = cur_col; i < cols; i++)
        cell_ch[cur_row][i] = 0;
    redraw();
}

void console_puts(const char *s)
{
    if (s == 0)
        return;
    while (*s)
        console_putc(*s++);
}

void console_backspace(void)
{
    console_putc('\b');
}

void console_cursor_get(int *col, int *row)
{
    if (col) *col = cur_col;
    if (row) *row = cur_row;
}

void console_cursor_set(int col, int row)
{
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col > cols) col = cols;
    if (row > rows) row = rows;
    cur_col = col;
    cur_row = row;
}

void console_clear_to_eol(void)
{
    int i;

    for (i = cur_col; i < cols; i++)
        cell_ch[cur_row][i] = 0;
    redraw();
}

int console_cols(void) { return cols; }
int console_rows(void) { return rows; }
