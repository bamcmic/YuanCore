/* shell/shell.c - YuanCore 命令行
 *
 * 特性：
 *   - 注册制命令（加命令见 shell.h 顶部说明）
 *   - 行编辑：左右移动光标、插入/退格/Delete、Home/End
 *   - 快捷键：Tab 补全、上下键历史、Ctrl+C 取消、Ctrl+L 清屏、Ctrl+U 清行、Esc 清行
 *   - 有 UI 窗口打开时进入模态，按键转给窗口（Esc 关闭）
 */
#include "shell.h"

#include "console.h"
#include "ui.h"

#include "../mode/kbd.h"
#include "../mode/mouse.h"
#include "../mode/timer.h"
#include "../mode/rtc.h"
#include "../mode/pmm.h"
#include "../mode/heap.h"
#include "../mode/io.h"
#include "../mode/multiboot.h"
#include "../mode/desktop.h"
#include "../mode/exec.h"
#include "registry.h"

#define LINE_MAX 160
#define CMD_MAX  24
#define HIST_MAX 16
#define PROMPT   "YuanCore> "
#define PROMPT_LEN 10

static struct shell_command commands[CMD_MAX];
static int                  command_count;

static char line[LINE_MAX];
static int  line_len;
static int  cursor_pos;
static int  prompt_col, prompt_row;

static char history[HIST_MAX][LINE_MAX];
static int  hist_count;
static int  hist_pos;          /* == hist_count 表示没在翻历史 */

static const struct multiboot_info *mbi_ref;

static int str_eq(const char *a, const char *b);
static int str_eq_prefix(const char *s, const char *pre, int len);
static void prompt(void);

/* ---- 输出 ---- */

void shell_print(const char *s)
{
    console_puts(s);
}

static void put_dec(unsigned int v)
{
    char tmp[12];
    int i = 0;

    if (v == 0) {
        console_putc('0');
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        console_putc(tmp[--i]);
}

static void put_hex(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    char tmp[10];
    int i = 0;

    if (v == 0) {
        console_putc('0');
        return;
    }
    while (v > 0) {
        tmp[i++] = d[v & 0xF];
        v >>= 4;
    }
    console_putc('0');
    console_putc('x');
    while (i > 0)
        console_putc(tmp[--i]);
}

/* ---- 注册 ---- */

void shell_register_command(const struct shell_command *cmd)
{
    if (cmd == 0 || cmd->name == 0 || cmd->fn == 0)
        return;
    if (command_count >= CMD_MAX)
        return;
    commands[command_count++] = *cmd;
}

/* ---- 内置命令 ---- */

static void cmd_help(const char *args)
{
    int i;

    (void)args;
    console_puts("Commands:\n");
    for (i = 0; i < command_count; i++) {
        console_puts("  ");
        console_puts(commands[i].name);
        console_puts(" - ");
        console_puts(commands[i].help);
        console_puts("\n");
    }
    console_puts("Keys: Tab complete, Up/Down history, Ctrl+C cancel, ");
    console_puts("Ctrl+L clear, Ctrl+U kill line\n");
}

static void cmd_clear(const char *args)
{
    (void)args;
    console_clear();
}

static void cmd_uptime(const char *args)
{
    (void)args;
    put_dec(timer_uptime_sec());
    console_puts(" seconds\n");
}

static void cmd_ticks(const char *args)
{
    (void)args;
    put_dec(timer_ticks());
    console_puts(" ticks\n");
}

static void cmd_mem(const char *args)
{
    void *p;

    (void)args;
    console_puts("Physical memory (bitmap PMM):\n");
    console_puts("  total  "); put_dec(pmm_total_kb()); console_puts(" KB\n");
    console_puts("  used   "); put_dec(pmm_used_kb());  console_puts(" KB\n");
    console_puts("  free   "); put_dec(pmm_free_kb());  console_puts(" KB\n");

    console_puts("Kernel heap:\n");
    console_puts("  total  "); put_dec(heap_total_bytes()); console_puts(" bytes\n");
    console_puts("  used   "); put_dec(heap_used_bytes());   console_puts(" bytes\n");

    p = kmalloc(128);
    if (p != 0) {
        console_puts("  kmalloc(128) -> ");
        put_hex((unsigned int)(unsigned long)p);
        console_puts("\n");
        kfree(p);
        console_puts("  kfree ok, used back to ");
        put_dec(heap_used_bytes());
        console_puts(" bytes\n");
    }
}

static void cmd_about(const char *args)
{
    (void)args;
    console_puts("YuanCore - a 32-bit x86 hobby kernel\n");
    console_puts("Boot     : GRUB Multiboot\n");
    console_puts("Graphics : linear framebuffer, 1024x768x32\n");
    console_puts("Features : GDT, IDT, PIC, PIT timer, PS/2 keyboard,\n");
    console_puts("           physical frame allocator, kernel heap,\n");
    console_puts("           windowed UI toolkit\n");
    if (mbi_ref != 0) {
        console_puts("Framebuffer : 0x");
        put_hex((unsigned int)mbi_ref->framebuffer_addr);
        console_puts("\n");
    }
}

static void cmd_reboot(const char *args)
{
    (void)args;
    /* 8042 键盘控制器的 CPU 复位命令 */
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);
    for (;;)
        hlt();
}

static void cmd_date(const char *args)
{
    struct rtc_time t;

    (void)args;
    if (rtc_read(&t) == 0) {
        put_dec((unsigned int)t.year);   console_putc('-');
        if (t.month < 10) console_putc('0');
        put_dec((unsigned int)t.month);  console_putc('-');
        if (t.day < 10) console_putc('0');
        put_dec((unsigned int)t.day);    console_puts("  ");
        if (t.hour < 10) console_putc('0');
        put_dec((unsigned int)t.hour);   console_putc(':');
        if (t.min < 10) console_putc('0');
        put_dec((unsigned int)t.min);    console_putc(':');
        if (t.sec < 10) console_putc('0');
        put_dec((unsigned int)t.sec);    console_puts("  (RTC)\n");
    } else {
        console_puts("rtc unavailable, uptime is ");
        put_dec(timer_uptime_sec());
        console_puts(" s\n");
    }
}

static void register_builtin_commands(void)
{
    static const struct shell_command builtins[] = {
        { "help",   "show this help",                    cmd_help   },
        { "clear",  "clear the console",                 cmd_clear  },
        { "about",  "kernel and display information",    cmd_about  },
        { "mem",    "physical memory / heap statistics", cmd_mem    },
        { "uptime", "seconds since boot",                cmd_uptime },
        { "ticks",  "raw timer tick count",              cmd_ticks  },
        { "date",   "real time from the CMOS clock",     cmd_date   },
        { "reboot", "reset the machine",                 cmd_reboot }
    };
    unsigned int i;

    for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        shell_register_command(&builtins[i]);
}

/* ---- 独立命令模块：在这里补一行 init 即可 ---- */
extern void cmd_settings_init(void);
extern void cmd_fs_init(void);
extern void cmd_mouse_init(void);
extern void cmd_exec_init(void);

/* ---- 字符串工具 ---- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int str_eq_prefix(const char *s, const char *pre, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (s[i] != pre[i])
            return 0;
    }
    return 1;
}

/* ---- 行编辑 ---- */

static int input_max(void)
{
    int m = console_cols() - PROMPT_LEN - 1;

    return (m < 20) ? 20 : m;
}

/* 把提示符+当前行重画一遍，再把光标放回编辑位置 */
static void line_render(void)
{
    console_cursor_set(prompt_col, prompt_row);
    console_clear_to_eol();
    console_puts(PROMPT);
    console_puts(line);
    console_cursor_set(prompt_col + PROMPT_LEN + cursor_pos, prompt_row);
}

static void line_reset(void)
{
    line_len = 0;
    cursor_pos = 0;
    line[0] = '\0';
    hist_pos = hist_count;
}

static void line_insert(char c)
{
    int i;

    if (line_len >= input_max() || line_len + 1 >= LINE_MAX)
        return;

    for (i = line_len; i > cursor_pos; i--)
        line[i] = line[i - 1];
    line[cursor_pos++] = c;
    line_len++;
    line[line_len] = '\0';
    line_render();
}

static void line_backspace(void)
{
    int i;

    if (cursor_pos == 0)
        return;
    for (i = cursor_pos - 1; i < line_len - 1; i++)
        line[i] = line[i + 1];
    line_len--;
    cursor_pos--;
    line[line_len] = '\0';
    line_render();
}

static void line_delete(void)
{
    int i;

    if (cursor_pos >= line_len)
        return;
    for (i = cursor_pos; i < line_len - 1; i++)
        line[i] = line[i + 1];
    line_len--;
    line[line_len] = '\0';
    line_render();
}

static void line_kill(void)
{
    line_len = 0;
    cursor_pos = 0;
    line[0] = '\0';
    line_render();
}

static void line_load(const char *s)
{
    int n = 0;

    while (s[n] != 0 && n < input_max() && n < LINE_MAX - 1) {
        line[n] = s[n];
        n++;
    }
    line[n] = '\0';
    line_len = n;
    cursor_pos = n;
    line_render();
}

static void history_push(const char *s)
{
    int i;

    if (s[0] == '\0')
        return;
    /* 与上一条相同就不重复记 */
    if (hist_count > 0 && str_eq(history[hist_count - 1], s))
        return;

    if (hist_count < HIST_MAX) {
        for (i = 0; s[i] != 0 && i < LINE_MAX - 1; i++)
            history[hist_count][i] = s[i];
        history[hist_count][i] = '\0';
        hist_count++;
    } else {
        /* 满了就整体前移 */
        for (i = 0; i < HIST_MAX - 1; i++) {
            int j;
            for (j = 0; history[i + 1][j] != 0; j++)
                history[i][j] = history[i + 1][j];
            history[i][j] = '\0';
        }
        for (i = 0; s[i] != 0 && i < LINE_MAX - 1; i++)
            history[HIST_MAX - 1][i] = s[i];
        history[HIST_MAX - 1][i] = '\0';
    }
    hist_pos = hist_count;
}

static void history_show_prev(void)
{
    if (hist_count == 0)
        return;
    if (hist_pos > 0)
        hist_pos--;
    line_load(history[hist_pos]);
}

static void history_show_next(void)
{
    if (hist_count == 0)
        return;
    if (hist_pos < hist_count) {
        hist_pos++;
        if (hist_pos >= hist_count) {
            hist_pos = hist_count;
            line_kill();
        } else {
            line_load(history[hist_pos]);
        }
    }
}

/* Tab 补全：对第一个词做前缀匹配 */
static void tab_complete(void)
{
    int i, matches = 0, common, len;
    const char *cand = 0;

    if (cursor_pos != line_len || line_len == 0)
        return;                     /* 只对行尾的完整单词补全 */

    for (i = 0; i < command_count; i++) {
        if (str_eq_prefix(commands[i].name, line, line_len)) {
            matches++;
            cand = commands[i].name;
        }
    }

    if (matches == 0)
        return;

    if (matches == 1) {
        len = 0;
        while (cand[len] != 0)
            len++;
        for (; line_len < len; line_len++)
            line[line_len] = cand[line_len];
        line[line_len] = '\0';
        cursor_pos = line_len;
        line_render();
        return;
    }

    /* 多个候选：补到公共前缀，然后列出 */
    common = line_len;
    for (;;) {
        char c = cand[common];
        int ok = (c != 0);

        if (ok) {
            for (i = 0; i < command_count; i++) {
                if (str_eq_prefix(commands[i].name, line, line_len)
                    && commands[i].name[common] != c) {
                    ok = 0;
                    break;
                }
            }
        }
        if (!ok)
            break;
        common++;
    }
    for (; line_len < common; line_len++)
        line[line_len] = cand[line_len];
    line[line_len] = '\0';
    cursor_pos = line_len;

    console_putc('\n');
    for (i = 0; i < command_count; i++) {
        if (str_eq_prefix(commands[i].name, line, line_len)) {
            console_puts("  ");
            console_puts(commands[i].name);
        }
    }
    console_putc('\n');
    prompt();
    line_render();
}

/* ---- 执行 ---- */

static void exec(char *l)
{
    int i, name_len;

    while (*l == ' ')
        l++;
    if (*l == 0)
        return;

    for (i = 0; i < command_count; i++) {
        const char *n = commands[i].name;

        name_len = 0;
        while (n[name_len] != 0)
            name_len++;

        if (str_eq(l, n)) {
            commands[i].fn(l + name_len);
            return;
        }
        if (l[name_len] == ' ' && str_eq_prefix(l, n, name_len)) {
            commands[i].fn(l + name_len + 1);
            return;
        }
    }

    console_puts("unknown command: ");
    console_puts(l);
    console_puts("\n");
}

static void prompt(void)
{
    console_cursor_get(&prompt_col, &prompt_row);
    console_puts(PROMPT);
}

void shell_redraw_ui(void)
{
    ui_refresh();
}

/* ---- 桌面点击启动（ui.c 的图标/Start 回调） ---- */

/* 桌面小程序（app_desktop.c） */
extern void app_files_open(void);
extern void app_about_open(void);
extern void app_startmenu_open(void);
extern void app_settings_open(void);

void shell_desktop_launch(int id)
{
    switch (id) {
    case 0:  app_files_open();      break;
    case 1:  /* Terminal：重开 shell 窗口并回到终端 */
        desktop_terminal_show(1);
        ui_close_all();
        ui_refresh();
        break;
    case 2:  app_settings_open();   break;
    case 3:  app_about_open();      break;
    case 4:  app_startmenu_open();  break;
    default: break;
    }
}

/* ---- 任务栏时钟的文本来源：RTC 优先，坏了退回 uptime ---- */

static void shell_clock_provider(char *buf, int max)
{    struct rtc_time t;

    if (rtc_read(&t) == 0) {
        buf[0] = (t.hour < 10) ? '0' : (char)('0' + t.hour / 10);
        buf[1] = (char)('0' + t.hour % 10);
        buf[2] = ':';
        buf[3] = (t.min < 10) ? '0' : (char)('0' + t.min / 10);
        buf[4] = (char)('0' + t.min % 10);
        buf[5] = ':';
        buf[6] = (t.sec < 10) ? '0' : (char)('0' + t.sec / 10);
        buf[7] = (char)('0' + t.sec % 10);
        buf[8] = '\0';
        return;
    }
    /* RTC 异常：显示开机时长 */
    {
        unsigned int s = timer_uptime_sec();
        unsigned int h = s / 3600, m = (s % 3600) / 60;

        buf[0] = (h < 10) ? '0' : (char)('0' + h / 10);
        buf[1] = (char)('0' + h % 10);
        buf[2] = ':';
        buf[3] = (m < 10) ? '0' : (char)('0' + m / 10);
        buf[4] = (char)('0' + m % 10);
        buf[5] = ':';
        s %= 60;
        buf[6] = (s < 10) ? '0' : (char)('0' + s / 10);
        buf[7] = (char)('0' + s % 10);
        buf[8] = '\0';
    }
    (void)max;
}

void shell_install_clock(void)
{
    desktop_set_clock_provider(shell_clock_provider);
}

void shell_init(void)
{
    mbi_ref = desktop_mbi();

    register_builtin_commands();
    cmd_settings_init();          /* <- 新命令模块在这里加一行 */
    cmd_fs_init();
    cmd_mouse_init();
    cmd_exec_init();

    /* 应用注册表：开始菜单检索 / apps 命令 / int 0x80 查询共用 */
    registry_set_launcher(exec_run);
    registry_init();
    registry_command_init();

    console_puts("YuanCore shell - type 'help' for commands.\n");
    line_reset();
    prompt();
    ui_mouse_init();              /* 光标初始位置居中 */
    ui_set_launch(shell_desktop_launch);
}

void shell_run(void)
{
    for (;;) {
        int c = kbd_poll();
        int dx, dy, btn;

        /* 先处理鼠标：移动光标 / 点击（图标、Start、×、窗口内按钮） */
        while (mouse_poll(&dx, &dy, &btn))
            ui_mouse(dx, dy, btn);

        /* 实验性:全局刷新 ~30fps(exp refresh)。PIT 100Hz,
         * 帧号 = tick*30/100,帧号变化即重绘整屏。空闲时靠 timer 中断
         * 唤醒 hlt,不影响功耗模型。 */
        if (ui_exp_refresh_on()) {
            static unsigned int last_frame;
            unsigned int frame = timer_ticks() * 30u / 100u;

            if (frame != last_frame) {
                last_frame = frame;
                ui_draw();
            }
        }

        /* 每秒刷新任务栏时钟（内部判断秒没变就直接返回） */
        ui_clock_tick();

        if (c < 0) {
            hlt();                  /* 都没事件就睡，中断会唤醒 */
            continue;
        }

        if (c == 0)
            continue;

        /* Win 键：唤起/收起任务栏（任何状态下都响应） */
        if (c == KEY_WIN) {
            desktop_taskbar_toggle();
            ui_refresh();
            continue;
        }

        /* 有 UI 窗口时进入模态，按键交给窗口 */
        if (ui_active()) {
            ui_key(c);
            continue;
        }

        switch (c) {
        case KEY_ENTER:
            console_putc('\n');
            line[line_len] = '\0';
            history_push(line);
            exec(line);
            line_reset();
            prompt();
            break;

        case KEY_BACKSPACE:
            line_backspace();
            break;

        case KEY_DELETE:
            line_delete();
            break;

        case KEY_LEFT:
            if (cursor_pos > 0) {
                cursor_pos--;
                line_render();
            }
            break;

        case KEY_RIGHT:
            if (cursor_pos < line_len) {
                cursor_pos++;
                line_render();
            }
            break;

        case KEY_HOME:
            cursor_pos = 0;
            line_render();
            break;

        case KEY_END:
            cursor_pos = line_len;
            line_render();
            break;

        case KEY_UP:
            history_show_prev();
            break;

        case KEY_DOWN:
            history_show_next();
            break;

        case KEY_TAB:
            tab_complete();
            break;

        case 0x03:                              /* Ctrl+C */
            console_puts("^C\n");
            line_reset();
            prompt();
            break;

        case 0x0C:                              /* Ctrl+L */
            console_clear();
            prompt();
            line_render();
            break;

        case 0x15:                              /* Ctrl+U */
        case KEY_ESC:
            line_kill();
            break;

        default:
            if (c >= 32 && c < 127)
                line_insert((char)c);
            break;
        }
    }
}
