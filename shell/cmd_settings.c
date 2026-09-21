/* shell/cmd_settings.c - settings 命令：UI / 色彩设置
 *
 * 这是"独立命令模块"的范例：
 *   - 只暴露一个 cmd_settings_init()
 *   - 在 shell/shell.c 的 shell_init() 里登记一行即可生效
 *
 * 用法：
 *   settings                       列出所有颜色项与当前值
 *   settings set <key> <RRGGBB>    改单项颜色，如 settings set titlebar 2E6CB0
 *   settings preset <name>         应用预置主题(default/ocean/sunset/matrix/light)
 *   settings presets               列出可用预置主题
 *   settings reset                 恢复 default 预置
 */
#include "shell.h"

#include "console.h"
#include "ui.h"
#include "exp.h"

#include "../mode/theme.h"
#include "../mode/fb.h"
#include "../mode/fs.h"

static void print_usage(void)
{
    console_puts("Usage:\n");
    console_puts("  settings                       list all color keys\n");
    console_puts("  settings set <key> <RRGGBB>    set one color\n");
    console_puts("  settings preset <name>         apply a preset theme\n");
    console_puts("  settings presets               list preset themes\n");
    console_puts("  settings wallpaper <style>     gradient / solid / dots\n");
    console_puts("  settings clock on|off          taskbar clock\n");
    console_puts("  settings ui                    open the theme window\n");
    console_puts("  settings exp [name] on|off     experimental options\n");
    console_puts("  settings reset                 restore default preset\n");
}

/* 图形化设置程序（shell/app_settings.c） */
extern void app_settings_open(void);

static void list_keys(void)
{
    int i;
    char hex[8];

    console_puts("Color keys:\n");
    for (i = 0; i < THEME_COLOR_COUNT; i++) {
        console_puts("  ");
        console_puts(theme_key_name(i));
        /* 简单对齐 */
        {
            const char *n = theme_key_name(i);
            int len = 0;
            while (n[len] != 0)
                len++;
            while (len < 15) {
                console_putc(' ');
                len++;
            }
        }
        console_puts(": ");
        console_puts(theme_hex(i, hex));
        console_puts("\n");
    }
}

static void list_presets(void)
{
    const char *p = theme_first_preset();

    console_puts("Preset themes: ");
    while (p != 0) {
        console_puts(p);
        p = theme_next_preset(p);
        if (p != 0)
            console_puts(", ");
    }
    console_puts("\n");
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* 解析 6 位 RRGGBB，成功返回颜色，失败返回 -1 */
static int parse_rgb(const char *s)
{
    int v = 0, i;

    for (i = 0; i < 6; i++) {
        int d = hex_val(s[i]);
        if (d < 0)
            return -1;
        v = (v << 4) | d;
    }
    return v;
}

static char *skip_spaces(char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

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

/* 取出第一个空格分隔的 token，返回剩余部分 */
static char *next_token(char *s, char *out, int out_max)
{
    int n = 0;

    s = skip_spaces(s);
    while (*s != 0 && *s != ' ') {
        if (n < out_max - 1)
            out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    return skip_spaces(s);
}

static void apply_and_report(const char *what)
{
    shell_redraw_ui();
    if (theme_save() == 0)
        console_puts(what);
    else
        console_puts(what);
    console_puts(" applied");
    if (fs_ready())
        console_puts(" (saved to /settings.cfg)");
    console_puts("\n");
}

void cmd_settings(const char *args)
{
    char sub[24];
    char a[24];
    char b[16];
    char *rest;

    rest = next_token((char *)args, sub, sizeof sub);

    if (sub[0] == '\0') {
        list_keys();
        print_usage();
        return;
    }

    if (str_eq(sub, "set")) {
        rest = next_token(rest, a, sizeof a);       /* key */
        rest = next_token(rest, b, sizeof b);       /* RRGGBB */

        if (a[0] == '\0' || b[0] == '\0') {
            console_puts("usage: settings set <key> <RRGGBB>\n");
            return;
        }
        {
            int key = theme_key_from_name(a);
            int rgb = parse_rgb(b);

            if (key < 0) {
                console_puts("unknown key: ");
                console_puts(a);
                console_puts("\n");
                return;
            }
            if (rgb < 0) {
                console_puts("bad color, expect 6 hex digits: ");
                console_puts(b);
                console_puts("\n");
                return;
            }
            theme_set(key, (unsigned int)rgb);
            apply_and_report(a);
        }
        return;
    }

    if (str_eq(sub, "preset")) {
        rest = next_token(rest, a, sizeof a);
        if (a[0] == '\0') {
            list_presets();
            return;
        }
        if (theme_preset(a) != 0) {
            console_puts("unknown preset: ");
            console_puts(a);
            console_puts("\n");
            list_presets();
            return;
        }
        apply_and_report("preset");
        return;
    }

    if (str_eq(sub, "presets")) {
        list_presets();
        return;
    }

    if (str_eq(sub, "wallpaper")) {
        rest = next_token(rest, a, sizeof a);

        if (str_eq(a, "gradient"))     theme_set_bg_style(THEME_WP_GRADIENT);
        else if (str_eq(a, "solid"))   theme_set_bg_style(THEME_WP_SOLID);
        else if (str_eq(a, "dots"))    theme_set_bg_style(THEME_WP_DOTS);
        else {
            console_puts("styles: gradient | solid | dots\n");
            return;
        }
        apply_and_report("wallpaper");
        return;
    }

    if (str_eq(sub, "clock")) {
        rest = next_token(rest, a, sizeof a);

        if (str_eq(a, "on"))           theme_set_clock_show(1);
        else if (str_eq(a, "off"))     theme_set_clock_show(0);
        else {
            console_puts("usage: settings clock on|off\n");
            return;
        }
        apply_and_report(a);
        return;
    }

    /* ---- 实验性选项（运行时生效，不持久化；表在 shell/exp.c） ---- */
    if (str_eq(sub, "exp")) {
        rest = next_token(rest, a, sizeof a);

        if (a[0] == '\0') {
            int i;

            console_puts("Experimental options:\n");
            for (i = 0; i < exp_count(); i++) {
                const struct exp_option *o = exp_get(i);
                int len = 0;

                console_puts("  ");
                console_puts(o->name);
                while (o->name[len] != 0) len++;
                while (len < 10) { console_putc(' '); len++; }
                console_puts(o->desc);
                console_puts("  [");
                console_puts(o->get() ? "on" : "off");
                console_puts("]\n");
            }
            console_puts("toggle: settings exp <name> on|off\n");
            return;
        }

        {
            int i = exp_find(a);

            if (i < 0) {
                console_puts("unknown option: ");
                console_puts(a);
                console_puts("\n");
                return;
            }
            {
                const struct exp_option *o = exp_get(i);

                rest = next_token(rest, b, sizeof b);

                if (str_eq(b, "on"))        o->set(1);
                else if (str_eq(b, "off"))  o->set(0);
                else {
                    console_puts("usage: settings exp ");
                    console_puts(o->name);
                    console_puts(" on|off\n");
                    return;
                }
                shell_redraw_ui();
                console_puts(o->name);
                console_puts(" -> ");
                console_puts(b);
                console_puts(" (experimental, not saved)\n");
            }
        }
        return;
    }

    if (str_eq(sub, "ui")) {
        app_settings_open();        /* 图形化设置窗口 */
        return;
    }

    if (str_eq(sub, "reset")) {
        theme_preset("default");
        apply_and_report("default preset");
        return;
    }

    console_puts("unknown subcommand: ");
    console_puts(sub);
    console_puts("\n");
    print_usage();
}

static const struct shell_command settings_cmd = {
    "settings", "UI: set / preset / wallpaper / clock / exp / ui", cmd_settings
};

void cmd_settings_init(void)
{
    shell_register_command(&settings_cmd);
}
