/* shell/cmd_fs.c - 文件系统命令
 *
 * 独立命令模块：cmd_fs_init() 在 shell/shell.c 里登记一行即可。
 */
#include "shell.h"

#include "console.h"

#include "../mode/fs.h"

static const char *skip_spaces(const char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

static void put_dec(int v)
{
    char t[12];
    int i = 0, j;

    if (v == 0) {
        console_putc('0');
        return;
    }
    if (v < 0) {
        console_putc('-');
        v = -v;
    }
    while (v > 0) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = 0; j < i; j++)
        console_putc(t[i - 1 - j]);
}

/* 取一个 token，返回剩余部分 */
static const char *next_arg(const char *s, char *out, int max)
{
    int n = 0;

    s = skip_spaces(s);
    while (*s != 0 && *s != ' ') {
        if (n < max - 1)
            out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    return skip_spaces(s);
}

static const char *fs_errstr(int e)
{
    switch (e) {
    case FS_NOTFOUND: return "not found";
    case FS_EXISTS:   return "already exists";
    case FS_NOSPACE:  return "no space (entry/file limit)";
    case FS_NOTEMPTY: return "directory not empty";
    case FS_ISDIR:    return "is a directory";
    default:          return "error";
    }
}

static void cmd_ls(const char *args)
{
    char path[64];
    const char *name;
    int idx = 0, is_dir, size;

    next_arg(args, path, sizeof path);
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }

    if (!fs_exists(path)) {
        console_puts("ls: not found: ");
        console_puts(path);
        console_puts("\n");
        return;
    }
    if (!fs_is_dir(path)) {
        console_puts(path);
        console_puts("  ");
        put_dec(fs_size(path));
        console_puts(" bytes\n");
        return;
    }

    while ((name = fs_list(path, idx, &is_dir, &size)) != 0) {
        console_puts(is_dir ? "  d " : "  - ");
        console_puts(name);
        if (!is_dir) {
            console_puts("  ");
            put_dec(size);
            console_puts(" bytes");
        }
        console_puts("\n");
        idx++;
    }
    if (idx == 0)
        console_puts("  (empty)\n");
}

static void cmd_cat(const char *args)
{
    char path[64];
    char buf[512];
    int len, i;

    next_arg(args, path, sizeof path);
    if (path[0] == '\0') {
        console_puts("usage: cat <file>\n");
        return;
    }

    len = fs_read(path, buf, (int)sizeof buf - 1);
    if (len < 0) {
        console_puts("cat: ");
        console_puts(fs_errstr(len));
        console_puts("\n");
        return;
    }
    buf[len] = '\0';
    for (i = 0; i < len; i++)
        console_putc(buf[i] == 0 ? ' ' : buf[i]);
    if (len > 0 && buf[len - 1] != '\n')
        console_putc('\n');
}

static void cmd_write(const char *args)
{
    char path[64];
    const char *text;
    int len = 0, r;

    args = next_arg(args, path, sizeof path);
    text = skip_spaces(args);

    if (path[0] == '\0' || text[0] == '\0') {
        console_puts("usage: write <file> <text>\n");
        return;
    }

    while (text[len] != 0)
        len++;

    r = fs_write(path, text, len);
    if (r != FS_OK) {
        console_puts("write: ");
        console_puts(fs_errstr(r));
        console_puts("\n");
        return;
    }
    console_puts("wrote ");
    console_puts(path);
    console_puts("\n");
}

static void cmd_mkdir(const char *args)
{
    char path[64];
    int r;

    next_arg(args, path, sizeof path);
    if (path[0] == '\0') {
        console_puts("usage: mkdir <dir>\n");
        return;
    }
    r = fs_create_dir(path);
    if (r != FS_OK) {
        console_puts("mkdir: ");
        console_puts(fs_errstr(r));
        console_puts("\n");
    }
}

static void cmd_rm(const char *args)
{
    char path[64];
    int r;

    next_arg(args, path, sizeof path);
    if (path[0] == '\0') {
        console_puts("usage: rm <path>   (目录必须为空)\n");
        return;
    }
    r = fs_delete(path);
    if (r != FS_OK) {
        console_puts("rm: ");
        console_puts(fs_errstr(r));
        console_puts("\n");
    }
}

static void cmd_df(const char *args)
{
    int used, total, bytes;

    (void)args;
    fs_stats(&used, &total, &bytes);
    console_puts("ramfs  entries: ");
    put_dec(used);
    console_puts("/");
    put_dec(total);
    console_puts("  bytes: ");
    put_dec(bytes);
    console_puts("\n");
}

static const struct shell_command fs_cmds[] = {
    { "ls",    "list directory",            cmd_ls    },
    { "cat",   "print a file",              cmd_cat   },
    { "write", "write text to a file",      cmd_write },
    { "mkdir", "create a directory",        cmd_mkdir },
    { "rm",    "delete a file/empty dir",   cmd_rm    },
    { "df",    "filesystem usage",          cmd_df    }
};

void cmd_fs_init(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(fs_cmds) / sizeof(fs_cmds[0]); i++)
        shell_register_command(&fs_cmds[i]);
}
