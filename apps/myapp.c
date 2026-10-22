/* myapp.c —— 由 YuanNode 节点式 IDE 生成
 * 目标：YuanCore ring3 用户程序（ELF64/x86-64，入口 app_main，int 0x80 系统调用）
 * 打包：编译后装进 .ycp 应用包（zip 仅 stored，见 api/ycp.md）再安装运行。
 * 不要手改本文件：改动请回到节点图，否则下次生成会被覆盖。
 */
#include "ycapi.h"

void app_main(void)
{
    yc_draw_window(320, 210, 400, 200, "YuanNode App");
    break;
    yc_exit(0);
}
