// kernel.c - 直接操作显存，让屏幕亮起来
void kmain(unsigned int magic, unsigned int addr) {
    // VGA 文本模式的显存基址 (0xB8000)
    char* video_memory = (char*) 0xB8000;
    const char* msg = "Hello, YuanCore Kernel is Alive!";

    // 清屏 (黑底黑字)
    for (int i = 0; i < 80 * 25 * 2; i++) {
        video_memory[i] = 0;
    }

    // 打印信息 (0x07 表示灰底黑字，你可以改成 0x4F 试试白底红字)
    int i = 0;
    while (msg[i] != '\0') {
        video_memory[i*2] = msg[i];
        video_memory[i*2 + 1] = 0x07;
        i++;
    }

    // 停住，别让 CPU 飞了
    while(1) {
        __asm__ volatile ("hlt");
    }
}