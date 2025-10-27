/*
 * YuanCore x86 微内核
 * 仅打印 "YuanCore" 的简单示例
 */

/* 屏幕显示相关定义 */
#define VIDEO_MEMORY 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define WHITE_ON_BLACK 0x0F

/* 端口操作函数 */
static inline void outb(unsigned short port, unsigned char value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* 屏幕光标控制 */
void enable_cursor(unsigned char cursor_start, unsigned char cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);
    
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

void update_cursor(int x, int y) {
    unsigned short pos = y * SCREEN_WIDTH + x;
    
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

/* 清屏函数 */
void clear_screen() {
    char *video_memory = (char*)VIDEO_MEMORY;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT * 2; i += 2) {
        video_memory[i] = ' ';
        video_memory[i + 1] = WHITE_ON_BLACK;
    }
}

/* 打印字符串函数 */
void print_string(const char *str, int x, int y) {
    char *video_memory = (char*)VIDEO_MEMORY;
    int offset = (y * SCREEN_WIDTH + x) * 2;
    
    for (int i = 0; str[i] != '\0'; i++) {
        video_memory[offset] = str[i];
        video_memory[offset + 1] = WHITE_ON_BLACK;
        offset += 2;
    }
}

/* 内核主函数 */
void kernel_main() {
    // 启用光标
    enable_cursor(14, 15);
    
    // 清屏
    clear_screen();
    
    // 在屏幕中央打印 "YuanCore"
    const char *message = "YuanCore";
    int message_len = 0;
    while (message[message_len] != '\0') message_len++;
    
    int x = (SCREEN_WIDTH - message_len) / 2;
    int y = SCREEN_HEIGHT / 2;
    
    print_string(message, x, y);
    
    // 更新光标位置
    update_cursor(x + message_len, y);
    
    // 无限循环，保持内核运行
    while (1) {
        // 空循环，保持内核运行
        asm volatile ("hlt");
    }
}