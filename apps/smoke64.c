/* apps/smoke64.c - ring3 冒烟自检程序(开机自动执行一次)
 *
 * 职责单一:证明 x64 ring3 全链路存活 --
 *   exec_run 载入 ELF64, user_enter64 切 ring3,
 *   int 0x80 系统调用(YC_PRINT), YC_EXIT 经 user_exit64 回内核。
 * print + exit 即退,不开窗不等事件,不阻塞启动。
 * 退出码 0 = 完整往返成功(内核串口打 X 信标)。
 */
#include "ycapi.h"

void app_main(void)
{
    yc_print("ring3 smoke: user program alive\n");
    yc_exit(0);
}
