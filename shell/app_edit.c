/* shell/app_edit.c - 文本编辑器窗口
 *
 * 由文件管理器对文本文件 "Edit"/单击打开：
 *   - 缓冲上限 EDIT_MAX(4096)；打开时超限自动只读
 *   - 可打印字符/Enter 插入，Backspace/Delete 删除，方向键移动
 *     (Up/Down 按列对齐跳行)
 *   - Ctrl+S(0x13) 或 Save 按钮写回 fs；Esc/Back 返回
 * 焦点模型：0=编辑区 1=Save 2=Back，Tab 循环(ui 层)。
 * 鼠标点按钮 = ui 层设焦点并补发 KEY_ENTER，走按钮分支 ——
 * (教训:曾把焦点 0/1 全给按钮,导致点击 Save/Back 都触发编辑区换行)
 */
#include "ui.h"

#include "console.h"

#include "../mode/kbd.h"
#include "../mode/fs.h"
#include "../mode/font.h"
#include "../mode/theme.h"

#define EDIT_MAX   4096
#define EDIT_ROWS  18
#define EDIT_CH_W  8            /* scale 1 字符宽 */

static int  edit_win = -1;
static char edit_path[80];
static char edit_buf[EDIT_MAX + 1];
static int  edit_len;
static int  edit_cur;               /* 光标(字符索引) */
static int  edit_ro;                /* 只读(文件超过缓冲上限) */
static int  edit_dirty;

static void edit_draw(int win);
static void edit_key(int win, int key);

static void edit_msg(const char *s)
{
    console_puts("[edit] ");
    console_puts(s);
    console_puts("\n");
}

/* 光标 -> (行, 列)。*line_off 返回该行首索引 */
static void edit_cursor_pos(int *row, int *col, int *line_off)
{
    int r = 0, c = 0, i, start = 0;

    for (i = 0; i < edit_cur && i < edit_len; i++) {
        if (edit_buf[i] == '\n') {
            r++;
            c = 0;
            start = i + 1;
        } else {
            c++;
        }
    }
    *row = r;
    *col = c;
    if (line_off != 0)
        *line_off = start;
}

void app_edit_open(const char *path)
{
    int i;

    for (i = 0; path[i] != 0 && i < (int)sizeof edit_path - 1; i++)
        edit_path[i] = path[i];
    edit_path[i] = '\0';

    edit_len = fs_read(edit_path, edit_buf, EDIT_MAX);
    if (edit_len < 0)
        edit_len = 0;
    edit_buf[edit_len] = '\0';

    edit_ro = (fs_size(edit_path) > EDIT_MAX);
    if (edit_ro)
        edit_len = (edit_len > EDIT_MAX) ? EDIT_MAX : edit_len;
    edit_cur = 0;
    edit_dirty = 0;

    if (ui_valid(edit_win)) {
        ui_refresh();
        return;
    }
    ui_focus_set(0);                  /* 从编辑区开始 */
    edit_win = ui_open(180, 60, 620, 440, "Editor", edit_draw, edit_key);
}

/* ---- 绘制 ---- */

static void edit_draw(int win)
{
    int x, y, cw, ch;
    int cur_row, cur_col, line_off;
    unsigned int fg = theme_get(THEME_CONSOLE_FG);

    ui_client(win, &x, &y, &cw, &ch);
    ui_focus_count(3);                /* 0=编辑区 1=Save 2=Back */

    ui_label(x, y, edit_path);
    if (edit_ro)
        ui_label(x + cw - 130, y, "[READ-ONLY]");
    else if (edit_dirty)
        ui_label(x + cw - 130, y, "[modified]");
    y += 24;

    edit_cursor_pos(&cur_row, &cur_col, &line_off);

    /* 从光标行附近取景,逐行画 EDIT_ROWS 行 */
    {
        int first_row = (cur_row >= EDIT_ROWS) ? cur_row - EDIT_ROWS / 2 : 0;
        int i = 0, r = 0;
        int row;

        while (r < first_row && i < edit_len) {
            if (edit_buf[i] == '\n')
                r++;
            i++;
        }

        for (row = 0; row < EDIT_ROWS && i <= edit_len; row++) {
            int e = i;
            char save;

            while (e < edit_len && edit_buf[e] != '\n')
                e++;

            if (r + row == cur_row) {
                /* 光标行:光标处字符 + 下划线 */
                if (i + cur_col <= edit_len) {
                    save = edit_buf[i + cur_col];
                    edit_buf[i + cur_col] = '\0';
                    ui_drawtext(x, y, edit_buf + i, fg, 1);
                    edit_buf[i + cur_col] = save;

                    {
                        int px = x + cur_col * EDIT_CH_W;
                        char cc[2];

                        cc[0] = (edit_cur < edit_len)
                              ? edit_buf[edit_cur] : ' ';
                        cc[1] = '\0';
                        ui_drawtext(px, y, cc, fg, 1);
                        cc[0] = '_';
                        ui_drawtext(px, y + 2, cc, fg, 1);
                    }
                }
            } else if (i < edit_len) {
                save = edit_buf[e];
                edit_buf[e] = '\0';
                ui_drawtext(x, y, edit_buf + i, fg, 1);
                edit_buf[e] = save;
            }
            y += 16;
            i = e + 1;
            if (i > edit_len)
                break;
        }
    }

    /* 底部按钮(焦点 1/2;编辑区是焦点 0) */
    ui_button(x, y + 6, (cw - 24) / 2, 30, "Save (Ctrl+S)", 1);
    ui_button(x + (cw - 24) / 2 + 24, y + 6, (cw - 24) / 2, 30, "Back", 2);
}

/* ---- 编辑操作 ---- */

static void edit_insert(char c)
{
    int i;

    if (edit_ro || edit_len >= EDIT_MAX)
        return;
    for (i = edit_len; i > edit_cur; i--)
        edit_buf[i] = edit_buf[i - 1];
    edit_buf[edit_cur++] = c;
    edit_len++;
    edit_buf[edit_len] = '\0';
    edit_dirty = 1;
}

static void edit_backspace(void)
{
    int i;

    if (edit_ro || edit_cur == 0)
        return;
    for (i = edit_cur - 1; i < edit_len - 1; i++)
        edit_buf[i] = edit_buf[i + 1];
    edit_len--;
    edit_cur--;
    edit_buf[edit_len] = '\0';
    edit_dirty = 1;
}

static void edit_delete(void)
{
    int i;

    if (edit_ro || edit_cur >= edit_len)
        return;
    for (i = edit_cur; i < edit_len - 1; i++)
        edit_buf[i] = edit_buf[i + 1];
    edit_len--;
    edit_buf[edit_len] = '\0';
    edit_dirty = 1;
}

/* 光标跳到上一行/下一行的同列位置 */
static void edit_vmove(int dir)
{
    int row, col, start;

    edit_cursor_pos(&row, &col, &start);

    if (dir < 0) {
        int prev_start, i;

        if (row == 0)
            return;
        prev_start = start - 1;           /* 上一行的 '\n' */
        i = prev_start;
        while (i > 0 && edit_buf[i - 1] != '\n')
            i--;
        {
            int c = 0;

            edit_cur = i;
            while (i + c < edit_len && edit_buf[i + c] != '\n' && c < col)
                c++;
            edit_cur = i + c;
        }
    } else {
        int i = start, next_start = -1, c;

        while (i < edit_len) {            /* 找下一行行首 */
            if (edit_buf[i] == '\n') {
                next_start = i + 1;
                break;
            }
            i++;
        }
        if (next_start < 0)
            return;
        c = 0;
        edit_cur = next_start;
        while (next_start + c < edit_len
               && edit_buf[next_start + c] != '\n' && c < col)
            c++;
        edit_cur = next_start + c;
    }
}

static void edit_save(void)
{
    int rc;

    if (edit_ro) {
        edit_msg("file too large for editor, not saved");
        return;
    }
    rc = fs_write(edit_path, edit_buf, edit_len);
    if (rc == FS_OK) {
        edit_dirty = 0;
        edit_msg("saved");
        edit_msg(edit_path);
        console_putc('\n');
    } else {
        edit_msg("save failed (fs error)");
    }
}

/* ---- 按键 ---- */

static void edit_key(int win, int key)
{
    (void)win;

    if (key == 0x13) {                    /* Ctrl+S:任何焦点下都保存 */
        edit_save();
        ui_refresh();
        return;
    }
    if (key == KEY_ESC) {
        ui_close(win);
        edit_win = -1;
        return;
    }

    /* 方向键永远移动光标(编辑器的肌肉记忆,不留给焦点切换) */
    switch (key) {
    case KEY_LEFT:
        if (edit_cur > 0)
            edit_cur--;
        ui_refresh();
        return;
    case KEY_RIGHT:
        if (edit_cur < edit_len)
            edit_cur++;
        ui_refresh();
        return;
    case KEY_UP:
        edit_vmove(-1);
        ui_refresh();
        return;
    case KEY_DOWN:
        edit_vmove(1);
        ui_refresh();
        return;
    default:
        break;
    }

    /* Enter:焦点在按钮则触发按钮,在编辑区(0)则换行 */
    if (key == KEY_ENTER) {
        int f = ui_focus();

        if (f == 1) {
            edit_save();
        } else if (f == 2) {
            ui_close(win);
            edit_win = -1;
            return;                       /* 窗口已关,别再刷新它 */
        } else if (!edit_ro) {
            edit_insert('\n');
        }
        ui_refresh();
        return;
    }

    if (key == KEY_BACKSPACE) {
        edit_backspace();
        ui_focus_set(0);
        ui_refresh();
        return;
    }
    if (key == KEY_DELETE) {
        edit_delete();
        ui_focus_set(0);
        ui_refresh();
        return;
    }
    if (key >= 32 && key < 127) {
        edit_insert((char)key);
        ui_focus_set(0);                  /* 打字即回编辑区 */
        ui_refresh();
        return;
    }
}
