/* apps/hello.c - YuanCore 用户程序示例（ring3）
 *
 * 程序自己持有主循环：画界面 → yc_wait_event 阻塞等事件 → 处理 → 重画。
 * 鼠标点击 / 回车 / Esc 都能退出。展示 ycapi.h 的典型用法。
 */
#include "ycapi.h"

#define WIN_X 320
#define WIN_Y 210
#define WIN_W 400
#define WIN_H 240

static int count;
static int focus;

/* 界面元素几何（每帧一致，点击命中要用） */
static int b1_x, b1_y, b1_w, b1_h;
static int b2_x, b2_y, b2_w, b2_h;

static void draw(void)
{
    int x, y, w, h;

    (void)h;
    yc_draw_window(WIN_X, WIN_Y, WIN_W, WIN_H, "Hello Program");
    x = WIN_X + 14;
    y = WIN_Y + 36 + 14;
    w = WIN_W - 28;
    h = WIN_H - 36 - 28;

    yc_label(x, y, "clicked:");
    {
        /* 按钮上方文字带计数 */
        static char txt[24];
        int i = 0;
        const char *p = "clicked: ";
        while (p[i]) { txt[i] = p[i]; i++; }
        if (count == 0) { txt[i++] = '0'; }
        else {
            char t[12]; int n = 0, v = count, j;
            while (v > 0) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
            for (j = 0; j < n; j++) txt[i++] = t[n - 1 - j];
        }
        txt[i] = '\0';
        yc_label(x, y + 20, txt);
    }

    b1_x = x;     b1_y = y + 48; b1_w = w; b1_h = 32;
    b2_x = x;     b2_y = y + 90; b2_w = w; b2_h = 32;

    yc_button(b1_x, b1_y, b1_w, b1_h, "Click me", focus == 0);
    yc_button(b2_x, b2_y, b2_w, b2_h, "Close",    focus == 1);

    yc_label(x, b2_y + b2_h + 12, "mouse click / arrows + Enter / Esc");
}

static int inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void app_main(void)
{
    int running = 1;

    count = 0;
    focus = 0;

    while (running) {
        int key = -1, dx, dy, btn;
        int type;
        int last_btn = 0;

        draw();

        /* 事件循环：处理到有"动作"为止再重画 */
        for (;;) {
            type = yc_wait_event(&key, &dx, &dy, &btn);

            if (type == 2) {
                /* 鼠标：按下沿做命中 */
                if ((btn & YC_BTN_LEFT) != 0 && (last_btn & YC_BTN_LEFT) == 0) {
                    int mx, my;
                    yc_cursor(&mx, &my);
                    if (inside(mx, my, b1_x, b1_y, b1_w, b1_h)) {
                        focus = 0;
                        count++;
                        break;              /* 触发 → 重画 */
                    }
                    if (inside(mx, my, b2_x, b2_y, b2_w, b2_h)) {
                        focus = 1;
                        running = 0;
                        break;
                    }
                }
                last_btn = btn;
                continue;
            }

            /* 键盘 */
            if (key == YC_KEY_ESC) {
                running = 0;
                break;
            }
            if (key == YC_KEY_UP || key == YC_KEY_LEFT) { focus = 0; break; }
            if (key == YC_KEY_DOWN || key == YC_KEY_RIGHT) { focus = 1; break; }
            if (key == YC_KEY_ENTER) {
                if (focus == 0) count++;
                else running = 0;
                break;
            }
        }
    }

    yc_exit(0);
}
