/* apps/closeall.c - YuanCore 用户程序（ring3）
 *
 * 关闭当前所有 UI 窗口，回到纯桌面+终端，然后退出。
 *
 * 运行（YuanCore shell 里）：
 *   run /apps/closeall.app
 *
 * 构建（WSL 或 make apps）：
 *   gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -c closeall.c -o closeall.o
 *   ld  -m elf_i386 -e app_main -Ttext-segment=0x400000 -o closeall.app closeall.o
 */
#include "ycapi.h"

void app_main(void)
{
    yc_close_all();
    yc_print("all windows closed\n");
    yc_exit(0);
}
