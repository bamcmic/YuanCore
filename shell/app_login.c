/* app_login.c - 登录/注册 + 首启全屏注册(第一部分 v3)
 *
 * A. 首启全屏注册(firstboot): 零账户时全屏 OOBE,注册前绕不过。
 *    键盘: 上下键/Tab 切输入框, Enter 提交, 退格删字。
 *    鼠标: 点击输入框切换焦点, 点击 Create account 提交。
 * B. 登录窗口: 有账户未登录 -> 模态窗口,Esc 关掉由守卫重开。
 *
 * 密码回显 '*';明文存 ramfs(见 users.h 的诚实声明)。
 */
#include "ui.h"

#include "users.h"

#include "../mode/theme.h"
#include "../mode/fb.h"
#include "../mode/font.h"
#include "console.h"
#include "logo_128.h"

#define INP_W      240
#define INP_H      26
#define LINE_H     (INP_H + 12)

/* ---- firstboot 面板布局(宏化: 绘制与鼠标命中共用一套坐标) ---- */
#define FB_PANEL_W  440
#define FB_PANEL_H  400
#define FB_LOGO_Y   14                  /* 面板内 logo 顶(128 高) */
#define FB_TITLE_Y  150
#define FB_NAME_LBL 184
#define FB_NAME_Y   204
#define FB_PASS_LBL 242
#define FB_PASS_Y   262
#define FB_BTN_Y    300
#define FB_BTN_H    30
#define FB_IN_X     40
#define FB_IN_W     240
#define FB_BTN_W    360

/* ---- 共享状态 ---- */
static char uname[16];
static char pass[16];

static void show_err(char *dst, const char *s)
{
    int i = 0;

    while (s[i] && i < 47) {
        dst[i] = s[i];
        i++;
    }
    dst[i] = '\0';
}

static void buf_key(char *buf, int key)
{
    int n = 0;

    while (buf[n])
        n++;
    if (key == KEY_BACKSPACE) {
        if (n > 0)
            buf[n - 1] = '\0';
        return;
    }
    if (key >= 32 && key < 127 && n < 15) {
        buf[n] = (char)key;
        buf[n + 1] = '\0';
    }
}

static void draw_input(int x, int y, const char *buf, int masked, int hot)
{
    unsigned int border = hot ? theme_get(THEME_ACCENT)
                              : theme_get(THEME_WINDOW_BORDER);
    unsigned int bg     = theme_get(THEME_WINDOW_BG);
    unsigned int fg     = theme_get(THEME_TEXT);
    char shown[17];
    int i;

    ui_panel(x, y, INP_W, INP_H, bg);
    ui_rect(x, y, INP_W, INP_H, border);

    for (i = 0; buf[i]; i++)
        shown[i] = masked ? '*' : buf[i];
    shown[i] = '_';
    shown[i + 1] = '\0';
    ui_drawtext(x + 8, y + 7, shown, fg, 1);
}

static void draw_logo_1bpp(int x, int y)
{
    int r, c;

    for (r = 0; r < LOGO_H; r++)
        for (c = 0; c < LOGO_W; c++)
            if (logo128_1bpp[r * 16 + (c >> 3)] & (0x80 >> (c & 7)))
                fb_putpixel(x + c, y + r, theme_get(THEME_TEXT));
}

/* ================= 首启全屏注册 ================= */

static int  fb_active;
static int  fb_phase;               /* 0=用户名 1=密码 2=提交按钮 */
static char fb_name[16];
static char fb_pass[16];
static char fb_err[48];
static int  mcx = -1, mcy = -1;     /* firstboot 自管光标(-1 = 未动过) */

int firstboot_blocking(void)
{
    return fb_active;
}

/* 面板左上角(绘制与鼠标命中共用) */
static void fb_panel_origin(int *px, int *py)
{
    *px = ((int)fb.width  - FB_PANEL_W) / 2;
    *py = ((int)fb.height - FB_PANEL_H) / 2;
}

static void firstboot_submit(void)
{
    int rc = users_add(fb_name, fb_pass);

    if (rc == 0) {
        users_set_current(fb_name);
        fb_active = 0;
        fb_err[0] = '\0';
        console_puts("\n[ok] admin account created - welcome, ");
        console_puts(fb_name);
        console_puts("\n");
        ui_refresh();                  /* 恢复桌面 + 终端 */
        return;
    }
    show_err(fb_err, rc == -1
             ? "bad name: a-z 0-9 _ (1..15), pass non-empty"
             : "fs error");
}

/* 全刷: 无条件整屏重绘(渐变+面板+内容+光标)。
 * 由 shell 按 ~30fps 帧门调用,事件即时,无需脏区跟踪。 */
void firstboot_repaint(void)
{
    ui_overlay_begin();
    int px, py;
    unsigned int panel  = theme_get(THEME_WINDOW_BG);
    unsigned int border = theme_get(THEME_WINDOW_BORDER);
    unsigned int fg     = theme_get(THEME_TEXT);
    unsigned int dim    = theme_get(THEME_TEXT_DIM);

    fb_gradient(theme_get(THEME_BG_TOP), theme_get(THEME_BG_BOTTOM));

    fb_panel_origin(&px, &py);
    ui_panel(px, py, FB_PANEL_W, FB_PANEL_H, panel);
    fb_rect(px, py, FB_PANEL_W, FB_PANEL_H, border);

    draw_logo_1bpp(px + (FB_PANEL_W - LOGO_W) / 2, py + FB_LOGO_Y);

    /* 标题: 真实宽度居中(v2 估算值导致字符偏移溢出面板) */
    {
        const char *title = "Create admin account";
        int tw = font_text_width(title, 2);

        font_drawtext(px + (FB_PANEL_W - tw) / 2, py + FB_TITLE_Y,
                      title, fg, 2);
    }

    font_drawtext(px + FB_IN_X, py + FB_NAME_LBL, "User name:", dim, 1);
    draw_input(px + FB_IN_X, py + FB_NAME_Y, fb_name, 0, fb_phase == 0);

    font_drawtext(px + FB_IN_X, py + FB_PASS_LBL, "Password:", dim, 1);
    draw_input(px + FB_IN_X, py + FB_PASS_Y, fb_pass, 1, fb_phase == 1);

    ui_draw_button(px + FB_IN_X, py + FB_BTN_Y, FB_BTN_W, FB_BTN_H,
                   "Create account", fb_phase == 2);

    if (fb_err[0])
        font_drawtext(px + FB_IN_X, py + FB_BTN_Y + FB_BTN_H + 8,
                      fb_err, theme_get(THEME_DANGER), 1);

    font_drawtext(px + FB_IN_X, py + FB_PANEL_H - 50,
                  "Up/Down or click to switch, Enter to continue",
                  dim, 1);
    font_drawtext(px + FB_IN_X, py + FB_PANEL_H - 32,
                  "YuanCore V00.00.11[ALPHE] - first boot setup",
                  dim, 1);

    /* 自管光标(十字准星) */
    if (mcx >= 0) {
        ui_hline(mcx - 6, mcy, 12, theme_get(THEME_ACCENT));
        ui_vline(mcx, mcy - 6, 12, theme_get(THEME_ACCENT));
    }

    ui_win_end();
}

void firstboot_key(int key)
{
    fb_err[0] = '\0';

    switch (key) {
    case KEY_UP:
    case KEY_LEFT:
        fb_phase = (fb_phase + 2) % 3;
        return;
    case KEY_DOWN:
    case KEY_RIGHT:
    case '\t':
        fb_phase = (fb_phase + 1) % 3;
        return;
    case KEY_ENTER:
        firstboot_submit();
        return;
    default:
        break;
    }

    if (fb_phase == 0)
        buf_key(fb_name, key);
    else if (fb_phase == 1)
        buf_key(fb_pass, key);
}

void firstboot_mouse(int dx, int dy, int btn)
{
    int px, py;

    if (mcx < 0) {                     /* 第一个鼠标事件: 初始化到中心 */
        mcx = (int)fb.width / 2;
        mcy = (int)fb.height / 2;
    }
    mcx += dx;
    mcy += dy;
    if (mcx < 0)                 mcx = 0;
    if (mcy < 0)                 mcy = 0;
    if (mcx >= (int)fb.width)    mcx = (int)fb.width - 1;
    if (mcy >= (int)fb.height)   mcy = (int)fb.height - 1;

    if ((btn & 1) == 0)
        return;

    fb_panel_origin(&px, &py);

    if (mcx >= px + FB_IN_X && mcx < px + FB_IN_X + FB_IN_W) {
        if (mcy >= py + FB_NAME_Y && mcy < py + FB_NAME_Y + INP_H) {
            fb_phase = 0;              /* 点用户名框 */
            return;
        }
        if (mcy >= py + FB_PASS_Y && mcy < py + FB_PASS_Y + INP_H) {
            fb_phase = 1;              /* 点密码框 */
            return;
        }
    }
    if (mcy >= py + FB_BTN_Y && mcy < py + FB_BTN_Y + FB_BTN_H &&
        mcx >= px + FB_IN_X && mcx < px + FB_IN_X + FB_BTN_W)
        firstboot_submit();            /* 点 Create account */
}

/* ================= 登录窗口(已有账户) ================= */

static int win_id = -1;
static int mode;                    /* 0=选用户 1=输密码 */
static char err_win[48];

static void try_login(void)
{
    int rc = users_check(uname, pass);

    if (rc == 0) {
        users_set_current(uname);
        err_win[0] = '\0';
        ui_close(win_id);
        console_puts("\n[ok] welcome, ");
        console_puts(uname);
        console_puts("\n");
        return;
    }
    show_err(err_win, rc == -1 ? "no such user" : "wrong password");
}

static void on_draw(int w)
{
    int x, y, cw, ch, i, n;

    ui_win_begin(w);
    ui_client_size(w, &cw, &ch);
    (void)ch;
    x = 0;
    y = 0;

    if (mode == 0) {
        n = users_count();
        ui_focus_count(n + 1);
        ui_label(x, y, "Select user:");
        y += 24;
        for (i = 0; i < n; i++)
            ui_button(x, y + i * LINE_H, cw - 90, INP_H,
                      users_get(i), i);
        y += n * LINE_H + 8;
        ui_button(x, y, cw - 90, INP_H, "New user...", n);
        return;
    }

    /* mode == 1: 密码 */
    ui_focus_count(2);
    ui_label(x, y, uname);
    y += 24;
    draw_input(x, y, pass, 1, 1);
    y += LINE_H + 8;
    ui_button(x, y, (cw - 90) / 2 - 4, INP_H, "Login", 1);
    ui_button(x + (cw - 90) / 2 + 4, y, (cw - 90) / 2 - 4, INP_H,
              "Back", 2);
    if (err_win[0])
        ui_drawtext(x, y + INP_H + 8, err_win,
                      theme_get(THEME_DANGER), 1);
}

static void on_key(int w, int key)
{
    int f = ui_focus();
    int n;

    (void)w;

    switch (key) {
    case KEY_UP:
    case KEY_LEFT:
        ui_focus_prev();
        break;
    case KEY_DOWN:
    case KEY_RIGHT:
        ui_focus_next();
        break;
    case KEY_ENTER:
    case ' ':
        if (mode == 0) {
            n = users_count();
            if (f < n) {
                int i;
                const char *nm = users_get(f);

                for (i = 0; nm[i]; i++)
                    uname[i] = nm[i];
                uname[i] = '\0';
                pass[0] = '\0';
                mode = 1;
                ui_focus_set(0);
                ui_refresh();
            } else if (f == n) {
                show_err(err_win,
                         "shell: reboot after deleting /users to re-run setup");
            }
        } else {
            if (f == 1)
                try_login();
            else if (f == 2) {
                mode = 0;
                err_win[0] = '\0';
                ui_focus_set(0);
                ui_refresh();
            }
        }
        break;
    default:
        if (mode == 1) {
            buf_key(pass, key);
            ui_refresh();
        }
        break;
    }
}

int app_login_open(void)
{
    if (ui_valid(win_id))
        return 0;
    if (!ui_active()) {
        mode = 0;
        err_win[0] = '\0';
        win_id = ui_open(440, 220, 380, 320,
                         "Login", on_draw, on_key);
        return win_id >= 0 ? 1 : -1;
    }
    return -1;
}

void app_login_autostart(void)
{
    if (users_count() == 0) {
        fb_active = 1;                 /* 首启: 全屏注册接管 */
        fb_name[0] = '\0';
        fb_pass[0] = '\0';
        fb_phase = 0;
        return;
    }
    if (users_current() == 0)
        app_login_open();
}
