/* theme.c - 主题/色彩设置
 *
 * 一张颜色表 + 几套预置。要加新的可配置颜色：
 *   1) 在 theme.h 的 enum 里补一项
 *   2) 在 defaults[] 和每个 preset 里补一列
 *   3) 在 key_names[] 补名字
 */
#include "theme.h"
#include "fs.h"

static unsigned int colors[THEME_COLOR_COUNT];

static const unsigned int defaults[THEME_COLOR_COUNT] = {
    /* BG_TOP */        0x1B3A5C,
    /* BG_BOTTOM */     0x0B1A2B,
    /* TASKBAR */       0x14283F,
    /* TASKBAR_LINE */  0x3D5A7D,
    /* TITLEBAR */      0x2E6CB0,
    /* TITLE_TEXT */    0xFFFFFF,
    /* WINDOW_BG */     0xF2F4F7,
    /* WINDOW_BORDER */ 0x8A94A6,
    /* TEXT */          0x1F242E,
    /* TEXT_DIM */      0x6B7482,
    /* ACCENT */        0x359E74,
    /* DANGER */        0xC03B3B,
    /* CONSOLE_FG */    0x1F242E,
    /* CONSOLE_BG */    0xF2F4F7,
    /* ICON_FILES */    0x3B82C4,
    /* ICON_TERMINAL */ 0x2C333D,
    /* ICON_SETTINGS */ 0x6B7482,
    /* ICON_ABOUT */    0x359E74
};

static const char *key_names[THEME_COLOR_COUNT] = {
    "bg_top", "bg_bottom", "taskbar", "taskbar_line",
    "titlebar", "title_text", "window_bg", "window_border",
    "text", "text_dim", "accent", "danger",
    "console_fg", "console_bg",
    "icon_files", "icon_terminal", "icon_settings", "icon_about"
};

/* ---- 预置主题 ---- */

struct preset {
    const char *name;
    const unsigned int *c;
};

static const unsigned int p_ocean[THEME_COLOR_COUNT] = {
    0x0E4C6E, 0x06222F, 0x0A2E40, 0x2C6B85,
    0x1D7FA8, 0xFFFFFF, 0xF0F7FA, 0x7FA6B5,
    0x12303C, 0x5B7A87, 0x2FA8A0, 0xB0452E,
    0x12303C, 0xEAF4F8,
    0x2E86AB, 0x1F3B4D, 0x5B7A87, 0x2FA8A0
};

static const unsigned int p_sunset[THEME_COLOR_COUNT] = {
    0x5C2E4A, 0x2B1024, 0x3A1A2E, 0x8A4A5C,
    0xC0563B, 0xFFF2E8, 0xFBF3EC, 0xA98A7C,
    0x3A1F18, 0x8A6A5C, 0xE0913A, 0xA8324A,
    0x3A1F18, 0xFBF3EC,
    0xC0563B, 0x4A2438, 0x8A6A5C, 0xE0913A
};

static const unsigned int p_matrix[THEME_COLOR_COUNT] = {
    0x001A00, 0x000500, 0x001200, 0x00B300,
    0x00B300, 0x001200, 0x001200, 0x008800,
    0x00E600, 0x009900, 0x00E600, 0xCC0000,
    0x00E600, 0x001200,
    0x007700, 0x004400, 0x006600, 0x00AA00
};

static const unsigned int p_light[THEME_COLOR_COUNT] = {
    0xE8ECF2, 0xC8D0DC, 0xD8DEE8, 0xA8B0BC,
    0x3D6FA8, 0xFFFFFF, 0xFFFFFF, 0xA8B0BC,
    0x20242E, 0x707884, 0x2E8B57, 0xB03A3A,
    0x20242E, 0xFFFFFF,
    0x4A90C4, 0x505A68, 0x8892A0, 0x2E8B57
};

static const struct preset presets[] = {
    { "default", defaults },
    { "ocean",   p_ocean   },
    { "sunset",  p_sunset  },
    { "matrix",  p_matrix  },
    { "light",   p_light   }
};

#define PRESET_COUNT (int)(sizeof(presets) / sizeof(presets[0]))

static int str_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return a == b;
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* ---- 实现 ---- */

void theme_init(void)
{
    int i;

    for (i = 0; i < THEME_COLOR_COUNT; i++)
        colors[i] = defaults[i];

    theme_load();       /* 有 /settings.cfg 就覆盖默认值（fs 未就绪时自动跳过） */
}

unsigned int theme_get(int key)
{
    if (key < 0 || key >= THEME_COLOR_COUNT)
        return 0;
    return colors[key];
}

int theme_set(int key, unsigned int rgb)
{
    if (key < 0 || key >= THEME_COLOR_COUNT)
        return -1;
    colors[key] = rgb & 0xFFFFFF;
    return 0;
}

int theme_key_from_name(const char *name)
{
    int i;

    if (name == 0)
        return -1;
    for (i = 0; i < THEME_COLOR_COUNT; i++) {
        if (str_eq(name, key_names[i]))
            return i;
    }
    return -1;
}

const char *theme_key_name(int key)
{
    if (key < 0 || key >= THEME_COLOR_COUNT)
        return 0;
    return key_names[key];
}

const char *theme_hex(int key, char *out)
{
    static const char *d = "0123456789ABCDEF";
    unsigned int v;
    int i;

    if (out == 0)
        return 0;

    v = theme_get(key);
    out[6] = '\0';
    for (i = 5; i >= 0; i--) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    return out;
}

int theme_preset(const char *name)
{
    int i, j;

    if (name == 0)
        return -1;
    for (i = 0; i < PRESET_COUNT; i++) {
        if (str_eq(name, presets[i].name)) {
            for (j = 0; j < THEME_COLOR_COUNT; j++)
                colors[j] = presets[i].c[j];
            return 0;
        }
    }
    return -1;
}

const char *theme_first_preset(void)
{
    return (PRESET_COUNT > 0) ? presets[0].name : 0;
}

const char *theme_next_preset(const char *cur)
{
    int i;

    if (cur == 0)
        return 0;
    for (i = 0; i < PRESET_COUNT; i++) {
        if (cur == presets[i].name)
            return (i + 1 < PRESET_COUNT) ? presets[i + 1].name : 0;
    }
    return 0;
}

/* ---- 持久化：/settings.cfg，每行 "key=RRGGBB" ---- */

#define THEME_CFG_PATH "/settings.cfg"

static void buf_put(char *buf, int max, int *off, const char *s)
{
    while (*s && *off < max - 1)
        buf[(*off)++] = *s++;
    buf[*off] = '\0';
}

int theme_save(void)
{
    char buf[640];
    int i, off = 0;

    if (!fs_ready())
        return -1;

    for (i = 0; i < THEME_COLOR_COUNT; i++) {
        char hex[8];

        buf_put(buf, sizeof buf, &off, theme_key_name(i));
        buf_put(buf, sizeof buf, &off, "=");
        buf_put(buf, sizeof buf, &off, theme_hex(i, hex));
        buf_put(buf, sizeof buf, &off, "\n");
    }

    return fs_write(THEME_CFG_PATH, buf, off);
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* 逐行解析 "key=RRGGBB"，只认认识的键，坏行忽略 */
static int parse_line(const char *buf, int start, int end)
{
    char name[32];
    int n = 0, t, i, rgb = 0;

    while (start + n < end && buf[start + n] != '=')
        n++;
    if (n <= 0 || n >= (int)sizeof name || start + n >= end)
        return -1;

    for (t = 0; t < n; t++)
        name[t] = buf[start + t];
    name[t] = '\0';

    for (i = 0; i < 6; i++) {
        int d = hex_val(buf[start + n + 1 + i]);

        if (d < 0)
            return -1;
        rgb = (rgb << 4) | d;
    }

    for (i = 0; i < THEME_COLOR_COUNT; i++) {
        if (str_eq(name, theme_key_name(i))) {
            colors[i] = (unsigned int)rgb;
            return 0;
        }
    }
    return -1;
}

int theme_load(void)
{
    char buf[640];
    int len, line_start, line_end;

    if (!fs_ready())
        return -1;
    if (!fs_exists(THEME_CFG_PATH))
        return -1;

    len = fs_read(THEME_CFG_PATH, buf, (int)sizeof buf - 1);
    if (len <= 0)
        return -1;
    buf[len] = '\0';

    for (line_start = 0; line_start < len; line_start = line_end + 1) {
        for (line_end = line_start;
             line_end < len && buf[line_end] != '\n'; line_end++)
            ;
        parse_line(buf, line_start, line_end);
    }
    return 0;
}
