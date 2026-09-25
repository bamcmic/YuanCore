/* x64/exec64.c - x86-64 版的 exec 稳定性闸门
 *
 * 与 i386 版 mode/exec.c 同接口。x86-64 版不迁移 ring3 切换
 * (i386 版该路径存在未定位根因的踩踏,详见 TECHNICAL.md),
 * 因此 exec_run 恒返回 EXEC_DISABLED —— 系统其它部分不受影响。
 */
#include "../mode/exec.h"

static int exec_enabled;

int exec_enabled_get(void) { return exec_enabled; }
void exec_set_enabled(int on) { exec_enabled = (on != 0); }

int exec_run(const char *path)
{
    (void)path;
    return EXEC_DISABLED;
}

int exec_last_status(void) { return -1; }
void exec_set_exit(int code) { (void)code; }
