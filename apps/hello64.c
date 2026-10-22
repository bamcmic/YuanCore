/* apps/hello64.c - x86-64 ring3 样例程序
 *
 * 构建见 API_SPEC.md §5(x86-64 一节)。
 * 运行:拷入 /apps 后 settings exp exec on,或经文件管理器点击。
 */
#include "ycapi.h"

void app_main(void)
{
    int k;

    yc_draw_window(300, 140, 400, 220, "Hello x64");
    yc_label(330, 190, "YuanCore x86-64 ring3 works!");
    yc_label(330, 216, "ESC = exit");
    yc_refresh();

    for (;;) {
        k = yc_getkey();
        if (k == YC_KEY_ESC || k == YC_KEY_ENTER)
            break;
    }
    yc_exit(0);
}
