/* shell/app_files.c - 文件管理器 v2
 *
 * 从 app_desktop.c 迁出并升级：
 *   - 单击/Enter 打开目录；文件按类型分发：
 *       .app  -> exec_run(双击运行; x64 下 ring3 未迁移会明确报错)
 *       .ypk  -> 自动解压到当前目录
 *       其他  -> 文本编辑器(app_edit)
 *   - 底部两行操作:
 *       [Up|Open|Edit|Close]
 *       [Del(确认式)|Zip|Unzip]
 *   - Zip 把选中目录打包成 <name>.ypk；Unzip 反向解包
 *   - 操作结果在窗口内消息行反馈(不依赖后面的控制台)
 */
#include "ui.h"

#include "../mode/ycp.h"
#include "registry.h"

#include "console.h"
#include "registry.h"

#include "../mode/kbd.h"
#include "../mode/fs.h"
#include "../mode/fsypk.h"
#include "../mode/exec.h"
#include "../mode/timer.h"
#include "../mode/theme.h"

extern void app_edit_open(const char *path);

#define FILES_MAX_ROWS 12
#define LIST_MAX       14
#define MSG_MAX        64

static int  files_win = -1;
static int  files_sel;                 /* 选中行(含 ".." 行) */
static int  files_scroll;
static ui_scroll fsc;               /* 列表滚动区(滚轮/滚动条拖动/缓动) */
static char files_cwd[80] = "/";
static char msg_buf[MSG_MAX];          /* 操作反馈(空=无) */
static int  del_armed;                 /* Del 二次确认 */

static char ent_name[LIST_MAX][FS_NAME_MAX + 2];
static int  ent_dir[LIST_MAX];
static int  ent_n;
static int  list_rows;

/* ---- 小工具 ---- */

static void scpy_cwd(const char *src)
{
    int n = 0;

    while (src[n] != 0 && n < (int)sizeof files_cwd - 1) {
        files_cwd[n] = src[n];
        n++;
    }
    files_cwd[n] = '\0';
}

static void path_join(const char *dir, const char *name, char *out, int max)
{
    int n = 0;

    while (dir[n] != 0 && n < max - 1) { out[n] = dir[n]; n++; }
    if (n > 0 && out[n - 1] != '/')
        out[n++] = '/';
    while (*name != 0 && n < max - 1)
        out[n++] = *name++;
    out[n] = '\0';
}

static void path_parent(const char *dir, char *out, int max)
{
    int n = 0, last = -1;

    while (dir[n] != 0 && n < max - 1) { out[n] = dir[n]; n++; }
    out[n] = '\0';
    if (n > 1) {
        int k = n - 1;

        if (out[k] == '/')
            out[k--] = '\0';
        for (; k > 0; k--) {
            if (out[k] == '/') {
                last = k;
                break;
            }
        }
        if (last > 0)
            out[last] = '\0';
        else {
            out[0] = '/';
            out[1] = '\0';
        }
    }
}

/* 大小写不敏感后缀匹配 */
static int ends_with_ci(const char *s, const char *suffix)
{
    int ls = 0, lf = 0, i, k;

    while (s[ls]) ls++;
    while (suffix[lf]) lf++;
    if (ls < lf)
        return 0;
    for (i = ls - lf, k = 0; k < lf; i++, k++) {
        char a = s[i], b = suffix[k];

        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static void set_msg(const char *s)
{
    int n = 0;

    while (s[n] != 0 && n < MSG_MAX - 1) {
        msg_buf[n] = s[n];
        n++;
    }
    msg_buf[n] = '\0';
}

/* 错误码 -> 短句 */
static const char *fs_errstr(int rc)
{
    switch (rc) {
    case FS_OK:       return "ok";
    case FS_ERR:      return "bad path";
    case FS_NOTFOUND: return "not found";
    case FS_EXISTS:   return "already exists";
    case FS_NOSPACE:  return "no space (entries/heap)";
    case FS_NOTEMPTY: return "dir not empty";
    case FS_ISDIR:    return "is a directory";
    case FS_FORMAT:   return "corrupt container";
    default:          return "fs error";
    }
}

/* ---- 目录列表 ---- */

static void files_load(void)
{
    int i;

    ent_n = 0;
    list_rows = 0;

    if (files_cwd[1] != '\0') {
        ent_name[0][0] = '.';
        ent_name[0][1] = '.';
        ent_name[0][2] = '\0';
        ent_dir[0] = 1;
        ent_n = 1;
    }

    for (i = 0; ent_n < LIST_MAX; i++) {
        const char *name = fs_list(files_cwd, i, &ent_dir[ent_n], 0);
        int n = 0;

        if (name == 0)
            break;
        while (name[n] != 0 && n < FS_NAME_MAX) {
            ent_name[ent_n][n] = name[n];
            n++;
        }
        ent_name[ent_n][n] = '\0';
        ent_n++;
    }

    list_rows = ent_n;
    if (files_sel >= list_rows)
        files_sel = list_rows - 1;
    if (files_sel < 0)
        files_sel = 0;
    if (files_scroll > files_sel)
        files_scroll = files_sel;
    if (files_scroll > list_rows - 1)
        files_scroll = (list_rows > 0) ? list_rows - 1 : 0;
}

/* 当前选中行(不含 "..")的绝对路径 */
static void selected_path(char *out, int max)
{
    int row = files_scroll + files_sel;

    if (row < 0 || row >= ent_n
        || (ent_dir[row] && ent_name[row][0] == '.')) {
        out[0] = '\0';
        return;
    }
    path_join(files_cwd, ent_name[row], out, max);
}

/* ---- 动作 ---- */

static void do_run_app(const char *path)
{
    int r = exec_run(path);

    if (r != EXEC_OK) {
        set_msg((r == EXEC_DISABLED)
                ? "run failed: ring3 disabled on x64"
                : "run failed: load error");
        console_puts("[files] exec failed: ");
        console_puts(path);
        console_puts(" (");
        if (r == EXEC_DISABLED)
            console_puts("ring3 disabled on x64");
        else
            console_puts("load error");
        console_puts(")\n");
    }
}

static void do_unzip(const char *path)
{
    int rc = fs_ypk_unpack(path, files_cwd);

    files_load();
    if (rc == FS_OK)
        set_msg("unpacked");
    else
        set_msg("unpack failed");
    console_puts("[files] unpack: ");
    console_puts(fs_errstr(rc));
    console_puts("\n");
}

static void do_zip(void)
{
    char sel[80], out[96];
    int i, base, rc;

    selected_path(sel, sizeof sel);
    if (sel[0] == '\0') {
        set_msg("select a directory first");
        return;
    }
    if (!fs_is_dir(sel)) {
        set_msg("zip works on directories");
        return;
    }

    /* 输出名 = 目录名 + ".ypk"(放当前目录) */
    base = 0;
    for (i = 0; sel[i] != 0; i++)
        if (sel[i] == '/')
            base = i + 1;
    {
        char name[FS_NAME_MAX + 8];
        int n = 0;

        while (sel[base] != 0 && n < FS_NAME_MAX)
            name[n++] = sel[base++];
        name[n] = '\0';
        path_join(files_cwd, name, out, sizeof out);
        n = 0;
        while (out[n] != 0)
            n++;
        out[n++] = '.'; out[n++] = 'y'; out[n++] = 'p'; out[n++] = 'k';
        out[n] = '\0';
    }

    rc = fs_ypk_pack(sel, out);
    files_load();
    if (rc == FS_OK)
        set_msg("packed");
    else
        set_msg("pack failed");
    console_puts("[files] pack: ");
    console_puts(fs_errstr(rc));
    console_puts("\n");
}

static void do_delete(void)
{
    char sel[80];
    int rc;

    selected_path(sel, sizeof sel);
    if (sel[0] == '\0') {
        set_msg("nothing selected");
        return;
    }
    rc = fs_rmtree(sel);
    files_load();
    del_armed = 0;
    if (rc == FS_OK)
        set_msg("deleted");
    else
        set_msg("delete failed");
    console_puts("[files] delete: ");
    console_puts(fs_errstr(rc));
    console_puts("\n");
}

/* 行点击/Enter:按类型分发 */
static void files_enter_row(int row)
{
    if (row < 0 || row >= ent_n)
        return;

    if (ent_dir[row] != 0) {
        if (ent_name[row][0] == '.' && ent_name[row][1] == '.') {
            path_parent(files_cwd, files_cwd, (int)sizeof files_cwd);
        } else {
            char next[80];

            path_join(files_cwd, ent_name[row], next, sizeof next);
            scpy_cwd(next);
        }
        files_sel = 0;
        files_scroll = 0;
        del_armed = 0;
        files_load();
        ui_refresh();
        return;
    }

    {
        char path[80];

        path_join(files_cwd, ent_name[row], path, sizeof path);
        if (ends_with_ci(ent_name[row], ".app")) {
            do_run_app(path);
            ui_refresh();
            return;
        }
        if (ends_with_ci(ent_name[row], ".ypk")) {
            do_unzip(path);
            ui_refresh();
            return;
        }
        if (ends_with_ci(ent_name[row], ".ycp")) {
            int rc = ycp_install(path);

            if (rc == 0) {
                set_msg("package installed");
                console_puts("[files] ycp installed: ");
                console_puts(path);
                console_puts("\n");
                registry_rescan();
            } else {
                set_msg("ycp install failed");
                console_puts("[files] ycp install failed (code ");
                {
                    char c1 = (char)('0' + ((-rc) / 10) % 10);
                    char c2 = (char)('0' + (-rc) % 10);
                    if (rc <= -10) console_putc(c1);
                    console_putc(c2);
                }
                console_puts("), see 'ycp' command\n");
            }
            files_load();
            ui_refresh();
            return;
        }
        del_armed = 0;
        app_edit_open(path);              /* 其余文件一律进编辑器 */
        return;
    }
}

/* ---- 绘制/按键 ---- */

static void files_draw(int win)
{
    (void)win;
    int x, y, cw, ch, i;
    int rows_h, bw1, bw2;

    ui_app_size(&cw, &ch);
    x = 0;
    y = 0;
    rows_h = (ch - 24 - 26 - 76) / 22;
    if (rows_h > FILES_MAX_ROWS) rows_h = FILES_MAX_ROWS;
    if (rows_h < 1) rows_h = 1;

    ui_focus_count(rows_h + 7);           /* 行 + 7 个按钮 */

    ui_label(x, y, files_cwd);
    y += 24;

    /* 滚动区: 滚轮/拖动右侧滚动条都会改 fsc, 绘制时与 files_scroll
     * 双向同步 —— 键盘上下用 files_scroll, 鼠标用 fsc, 最后以绘制
     * 这一帧为准(u_scroll_set 同时归位 target)。*/
    fsc.x = x + 2;
    fsc.y = y;
    fsc.w = cw - 26;
    fsc.h = rows_h * 22;
    fsc.content_h = list_rows * 22;
    ui_scroll_set(&fsc, files_scroll);
    files_scroll = fsc.offset;

    for (i = 0; i < rows_h; i++) {
        int row = files_scroll + i;
        char line[FS_NAME_MAX + 4];
        int n = 0;

        if (row >= list_rows)
            break;
        while (ent_name[row][n] != 0) {
            line[n] = ent_name[row][n];
            n++;
        }
        if (ent_dir[row] != 0 && !(n == 2 && ent_name[row][0] == '.'))
            line[n++] = '/';
        line[n] = '\0';
        ui_button(x, y, cw - 8, 20, line, i);
        y += 22;
    }

    ui_scroll_bar(&fsc);              /* 右侧滚动条(可拖) */

    /* 消息行 */
    if (msg_buf[0] != '\0')
        ui_label(x, y + 2, msg_buf);
    y += 26;

    /* 按钮第一行:Up | Open | Edit | Close */
    bw1 = (cw - 72) / 4;
    ui_button(x, y, bw1, 30, "Up", rows_h);
    ui_button(x + bw1 + 24, y, bw1, 30, "Open", rows_h + 1);
    ui_button(x + 2 * (bw1 + 24), y, bw1, 30, "Edit", rows_h + 2);
    ui_button(x + 3 * (bw1 + 24), y, cw - 3 * (bw1 + 24), 30, "Close",
              rows_h + 3);
    y += 38;

    /* 第二行:Del(确认式) | Zip | Unzip */
    bw2 = (cw - 48) / 3;
    ui_button(x, y, bw2, 30, del_armed ? "Sure?" : "Del", rows_h + 4);
    ui_button(x + bw2 + 24, y, bw2, 30, "Zip", rows_h + 5);
    ui_button(x + 2 * (bw2 + 24), y, cw - 2 * (bw2 + 24), 30, "Unzip",
              rows_h + 6);
}

static void files_key(int win, int key)
{
    int rows_h, base;

    {
        int cw, ch;

        ui_app_size(&cw, &ch);
        rows_h = (ch - 24 - 26 - 76) / 22;
        if (rows_h > FILES_MAX_ROWS) rows_h = FILES_MAX_ROWS;
        if (rows_h < 1) rows_h = 1;
        base = rows_h;
    }

    switch (key) {
    case KEY_UP:
        if (files_sel > 0)
            files_sel--;
        if (files_sel < files_scroll)
            files_scroll = files_sel;
        ui_focus_set(files_sel - files_scroll);
        ui_refresh();
        return;
    case KEY_DOWN:
        if (files_sel < list_rows - 1)
            files_sel++;
        if (files_sel >= files_scroll + rows_h)
            files_scroll = files_sel - rows_h + 1;
        ui_focus_set(files_sel - files_scroll);
        ui_refresh();
        return;
    case KEY_ESC:
        ui_close(win);
        return;
    default:
        break;
    }

    if (key == KEY_ENTER) {
        int f = ui_focus();
        char sel[80];

        if (f < base) {
            files_enter_row(files_scroll + f);
            return;
        }
        selected_path(sel, sizeof sel);

        f -= base;
        switch (f) {
        case 0:                           /* Up */
            if (files_cwd[1] != '\0') {
                path_parent(files_cwd, files_cwd, sizeof files_cwd);
                files_sel = 0;
                files_scroll = 0;
                del_armed = 0;
                files_load();
            }
            break;
        case 1:                           /* Open */
            files_enter_row(files_scroll + files_sel);
            return;
        case 2:                           /* Edit */
            if (sel[0] != '\0' && !fs_is_dir(sel))
                app_edit_open(sel);
            else
                set_msg("select a file to edit");
            break;
        case 3:                           /* Close */
            ui_close(win);
            return;
        case 4:                           /* Del:二次确认 */
            if (!del_armed) {
                del_armed = 1;
                set_msg("press Del again to confirm");
            } else {
                do_delete();
            }
            break;
        case 5:                           /* Zip */
            del_armed = 0;
            do_zip();
            break;
        case 6:                           /* Unzip */
            del_armed = 0;
            if (sel[0] != '\0' && ends_with_ci(sel, ".ypk"))
                do_unzip(sel);
            else
                set_msg("select a .ypk file");
            break;
        default:
            break;
        }
        ui_refresh();
        return;
    }
}

void app_files_open(void)
{
    if (ui_valid(files_win)) {
        ui_refresh();
        return;
    }
    files_sel = 0;
    files_scroll = 0;
    del_armed = 0;
    msg_buf[0] = '\0';
    files_load();
    files_win = ui_open(200, 50, 460, 480, "Files", files_draw, files_key);
}
