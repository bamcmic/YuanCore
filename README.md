📄 README.md
markdown

# YuanCore OS

一个从零开始构建的 32 位 x86 操作系统内核。基于 GRUB Multiboot 引导，使用 C 语言和 NASM 汇编编写，运行在 QEMU 虚拟机中。

## 📖 项目简介

YuanCore OS 是一个极简的操作系统内核，旨在提供最小化的内核开发框架。它直接操作 VGA 显存进行文本输出，不依赖任何标准库，是完全独立的自举系统。

### 特性

- ✅ 支持 GRUB Multiboot 引导
- ✅ 32 位保护模式
- ✅ 直接 VGA 文本模式输出 (0xB8000)
- ✅ 简易 `printf` 函数（支持字符串输出、颜色控制）
- ✅ 自动化构建系统（Makefile）
- ✅ 支持 `mode/` 目录扩展（多文件编译）

### 技术栈

| 组件 | 工具 | 说明 |
| :--- | :--- | :--- |
| 汇编器 | NASM | 编写启动引导代码 |
| 编译器 | GCC (i386-elf) | C 语言内核编译 |
| 链接器 | LD | ELF 格式链接 |
| 引导器 | GRUB | Multiboot 兼容引导 |
| 模拟器 | QEMU | 在虚拟机中运行测试 |
| 构建工具 | Make | 自动化构建流程 |

---

## 📂 目录结构

~/myos/
├── boot.asm # 启动引导汇编代码 (Multiboot 头部)
├── kernel.c # 内核主程序 (C 语言)
├── linker.ld # 链接脚本 (指定内存布局)
├── Makefile # 自动化构建文件
├── README.md # 项目文档
├── mode/ # (可选) 扩展模块目录
│ ├── extra1.c
│ └── extra2.c
└── iso/ # (构建时生成) ISO 镜像临时目录
text


---

## 🛠️ 构建方法

### 前提条件

确保你的系统已安装以下工具：

#### Ubuntu/Debian (包括 WSL)

```bash
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso

macOS (使用 Homebrew)
bash

brew install nasm qemu grub xorriso

Windows (WSL)

推荐使用 WSL2 并安装 Ubuntu 发行版，然后在 WSL 中执行上述 Ubuntu 命令。
快速构建
bash

# 1. 进入项目目录
cd ~/myos

# 2. 清理之前的构建产物 (可选)
make clean

# 3. 编译并生成 ISO 镜像
make

# 4. 在 QEMU 中运行
make run

完整构建流程
命令	说明
make	编译所有 .c 和 .asm 文件，生成 myos.iso
make clean	删除所有编译产物和临时文件
make run	在 QEMU 虚拟机中启动内核
make clean && make run	完全重新构建并运行
构建产物

    boot.o - 汇编代码编译后的目标文件

    kernel.o - C 代码编译后的目标文件 (以及 mode/ 下的所有 .o)

    mykernel.elf - 链接后的 ELF 可执行文件

    myos.iso - 可启动的 ISO 镜像文件 (最终产物)

🚀 运行
在 QEMU 中运行
bash

make run

或手动执行：
bash

qemu-system-i386 -cdrom myos.iso

在真机上运行 (高级)

    将 myos.iso 写入 U 盘：
    bash

    sudo dd if=myos.iso of=/dev/sdX bs=4M status=progress

    ⚠️ 注意：请将 /dev/sdX 替换为你的 U 盘设备名，操作会清空 U 盘数据！

    重启电脑，选择从 U 盘启动。

🎨 自定义修改
修改输出文字

编辑 kernel.c，找到 printf 调用：
c

printf("Hello, YuanCore Kernel is Alive!");
printf(" This is my custom printf!");

修改文字颜色

在 kernel.c 顶部修改 color 变量的值：
c

int color = 0x07;   // 灰底黑字 (默认)
// int color = 0x4F;  // 白底红字
// int color = 0x1F;  // 蓝底白字
// int color = 0x2F;  // 绿底白字

颜色属性字节格式：背景色(高4位) | 前景色(低4位)。
添加新模块

    在 mode/ 目录下创建 .c 文件（如 mode/math.c）

    在 kernel.c 中声明外部函数：
    c

    extern void my_function();

    在 kmain 中调用它

    运行 make，新文件会自动编译并链接

🐛 常见问题
问题：nasm: command not found

解决：安装 NASM 汇编器。
bash

sudo apt install nasm   # Ubuntu/Debian

问题：grub-mkrescue: command not found

解决：安装 GRUB 工具。
bash

sudo apt install grub-pc-bin xorriso   # Ubuntu/Debian

问题：QEMU 窗口黑屏，没有任何文字

解决：检查 kernel.c 中的 kmain 函数是否正确调用了 printf，以及清屏循环后是否有打印语句。
问题：make 报错 No rule to make target ...

解决：确保 Makefile 和源代码文件在同一个目录下，运行 make clean 后重试。
📚 后续扩展方向

    □

    添加中断处理 (键盘响应)
    □

    支持 64 位长模式 (x86_64)
    □

    内存管理 (分页和堆分配)
    □

    简单进程调度
    □

    文件系统支持
    □

    UEFI 引导支持 (使用 Limine)

📜 许可证

本项目仅供学习和研究使用，无特定许可证限制。
👨‍💻 作者

bamcmic - 操作系统爱好者
🙏 致谢

    OSDev Wiki - 操作系统开发权威参考资料

    GRUB Manual - Multiboot 标准文档

    QEMU Documentation - 虚拟机调试工具