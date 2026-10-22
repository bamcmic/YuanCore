/* splash.c - 开机 logo 动画
 *
 * 深色底 -> logo 128x128 alpha 渐入(24 帧, ~0.5s) -> 停留 ~0.6s。
 * logo 资源: logo_128.h(由 logo.png 生成, 墨色 #060303, 带 alpha)。
 * 混合只在 logo 不透明区域做, 背景色作为混合底色已知, 不需要读显存。
 */
#include "splash.h"

#include "fb.h"
#include "timer.h"
#include "io.h"
#include "logo_128.h"

#define SPLASH_BG   RGB(0x0B, 0x1A, 0x2B)   /* 与默认桌面底色衔接 */
#define FADE_STEPS  24
#define FRAME_TICKS 2                       /* ~20ms @ 100Hz */
#define HOLD_TICKS  300                      /* 3.0s @ 100Hz */
#define SPLASH_SCALE 3                       /* 128 -> 384 最近邻放大 */

static void wait_ticks(unsigned int n)
{
    unsigned int t = timer_ticks();

    while (timer_ticks() - t < n)
        hlt();
}

void splash_show(void)
{
    /* splash 的帧延时依赖 timer 中断。调用点在 fb_init 之后,此时
     * GDT/IDT/PIC/timer/kbd 均已初始化(信标 G I C N 均在 f 之前),
     * 内核主流程尚未 sti —— 这里提前开中断,否则 hlt 永睡。 */
    sti();

    int dw = LOGO_W * SPLASH_SCALE;
    int dh = LOGO_H * SPLASH_SCALE;
    int ox = ((int)fb.width  - dw) / 2;
    int oy = ((int)fb.height - dh) / 2 - 30;
    int step, x, y;

    fb_clear(SPLASH_BG);

    for (step = 1; step <= FADE_STEPS; step++) {
        unsigned int amax = (unsigned int)step * 255u / FADE_STEPS;

        for (y = 0; y < dh; y++) {
            const unsigned char *row =
                &logo128_rgba[(y / SPLASH_SCALE) * LOGO_W * 4];

            for (x = 0; x < dw; x++) {
                unsigned int a = ((unsigned int)row[(x / SPLASH_SCALE) * 4 + 3]
                                  * amax) / 255u;

                if (a == 0)
                    continue;               /* 背景已就位, 跳过 */
                {
                    unsigned int inv = 255u - a;
                    unsigned int r = (LOGO_INK_R * a + 0x0B * inv) / 255u;
                    unsigned int g = (LOGO_INK_G * a + 0x1A * inv) / 255u;
                    unsigned int b = (LOGO_INK_B * a + 0x2B * inv) / 255u;

                    fb_putpixel(ox + x, oy + y, RGB(r, g, b));
                }
            }
        }
        wait_ticks(FRAME_TICKS);
    }

    wait_ticks(HOLD_TICKS);
}
