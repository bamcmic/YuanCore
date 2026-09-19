# YuanCore（元核）

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**YuanCore** 是一个从零开始的 32 位 x86 操作系统内核，基于 GRUB Multiboot 引导，使用 C 语言和 NASM 汇编编写。项目旨在提供一个简洁、可扩展的内核学习与开发平台。

> ⚠️ **当前状态**：项目处于早期开发阶段，仅支持 i386 (32位) BIOS 引导，暂不支持 x86_64。

---

## 📖 项目简介

YuanCore 是一个面向所有人的操作系统内核，旨在提供更现代化的体验和更加便捷的创作体验。

### ✨ 功能特性

- ✅ GRUB Multiboot 引导支持
- ✅ 32 位保护模式（自建 GDT）
- ✅ **中断体系**：IDT + 48 个中断桩 + 8259 PIC 重映射，异常带完整现场打印
- ✅ **PIT 时钟**（100Hz）与 **PS/2 键盘**驱动（环形缓冲 + Shift）
- ✅ **内存管理**：基于 Multiboot 内存映射的物理帧位图分配器 + 内核堆(kmalloc/kfree)
- ✅ **图形模式**：通过 Multiboot video 请求 1024x768x32 线性帧缓冲
- ✅ 描点绘图原语：像素 / 水平垂直线 / 矩形填充描边 / 垂直渐变
- ✅ 8x16 点阵字体（Consolas 光栅化，95 个可打印 ASCII 字形）
- ✅ **图形桌面**：图标 + 窗口（标题栏/关闭钮）+ 任务栏 + **滚动控制台**
- ✅ **交互式 shell**（独立 `shell/` 目录，注册制加命令）：help / clear / about / mem /
  uptime / ticks / echo / **settings** / reboot
- ✅ **行编辑与快捷键**：光标移动、插入/删除、Home/End、上下键历史、
  Tab 补全、Ctrl+C/L/U、Esc
- ✅ **窗口 UI 工具包**：模态窗口 + 焦点管理 + 按钮/标签控件，
  图形化主题设置程序（`settings ui`，方向键 + Enter 换肤）
- ✅ **分页**：内核/用户/堆/帧缓冲分区映射，user 位即保护边界，用户态越界 → 页错误杀进程
- ✅ **ring3 用户态**：TSS + iret 切换（含段寄存器跳板），用户态异常杀程序不崩内核
- ✅ **int 0x80 系统调用**：18 个服务（打印/窗口/按键/文件/鼠标），程序与内核解耦
- ✅ **PS/2 鼠标**：IRQ12 三字节包驱动，屏幕光标（白箭头+投影），
  按钮可点击命中——现有程序零改动获得鼠标支持
- ✅ **程序加载执行**：标准 ELF32 可执行文件（基址 4MB，入口 app_main），
  内置演示程序 `run /apps/hello.app`
- ✅ **文件系统**：内存文件系统（目录树 + 堆存储），
  `ls` / `cat` / `write` / `mkdir` / `rm` / `df`；主题配置持久化到 `/settings.cfg`
- ✅ **性能**：32bpp 填充/画线直写快路径、渐变插值修正、UI 脏标记按需重绘
- ✅ **主题与设置**：18 个颜色项 + 5 套预置主题(default/ocean/sunset/matrix/light)，
  运行时改色即时生效
- ✅ VGA 文本模式输出（0xB8000，作为帧缓冲不可用与 panic 的回退）
- ✅ 支持 `mode/` 目录自动扩展编译（.c 与 .asm 都会自动收进构建）
- ✅ 自动化 Makefile 构建，支持 QEMU 运行 / 无头截图 / gdb 调试

---

## 🛠️ 技术栈

| 组件 | 工具 | 说明 |
| :--- | :--- | :--- |
| 汇编器 | NASM | 编写引导代码 |
| 编译器 | GCC (i386-elf) | C 语言内核编译 |
| 链接器 | LD | ELF 格式链接 |
| 引导器 | GRUB | Multiboot 兼容引导 |
| 模拟器 | QEMU | 虚拟机运行测试 |
| 构建工具 | Make | 自动化构建流程 |

---

## 📂 目录结构

```
YuanCore/
├── boot.asm          # GRUB Multiboot 引导入口（含图形模式请求）
├── kernel.c          # 内核主程序（初始化顺序在这里一目了然）
├── linker.ld         # 链接脚本（基址 1MB，导出 kernel_end）
├── Makefile          # 自动化构建（make / run / shot / debug / clean）
├── TECHNICAL.md      # 技术文档（架构 / 启动流程 / 内存布局 / 中断体系）
├── README.md         # 本文件
├── LICENSE           # GPL-3.0 许可证
└── mode/             # 内核模块
    ├── multiboot.h   # Multiboot 信息结构体（含编译期偏移断言）
    ├── registers.h   # 中断现场布局
    ├── io.h          # 端口读写
    ├── gdt.c/.h      # 全局描述符表
    ├── isr.asm       # 中断桩（异常 0-31 + IRQ 0-15）
    ├── idt.c/.h      # 中断描述符表与异常分发
    ├── irq.c/.h      # 8259 PIC 重映射与 IRQ 注册
    ├── timer.c/.h    # PIT 时钟（100Hz）
    ├── kbd.c/.h      # PS/2 键盘驱动
    ├── pmm.c/.h      # 物理帧位图分配器
    ├── heap.c/.h     # 内核堆 kmalloc/kfree
    ├── fs.c/.h       # 内存文件系统（目录树 + 堆存储）
    ├── fb.c/.h       # 帧缓冲驱动与绘图原语
    ├── font.c/.h     # 图形模式文本绘制
    ├── font_data.h   # 8x16 点阵字模（自动生成，勿手改）
    ├── console.c/.h  # 图形滚动控制台
    ├── theme.c/.h    # 主题/色彩设置（UI 唯一取色处）
    ├── desktop.c/.h  # 桌面外观与 UI 标准
    └── print.c/.h    # VGA 文本模式输出（回退/panic 用）
└── shell/            # 命令行与程序（扩展往这里加）
    ├── shell.c/.h    # 命令注册、行编辑、历史、快捷键
    ├── console.c/.h  # 图形滚动控制台
    ├── ui.c/.h       # 窗口 UI 工具包（给其它程序用）
    ├── app_settings.c # 图形化主题设置窗口（settings ui）
    ├── cmd_settings.c # settings 命令（新增命令的范例）
    └── cmd_fs.c      # ls / cat / write / mkdir / rm / df
```

---

## 🚀 构建与使用

### 前提条件

- **Linux**（推荐 Ubuntu）或 **Windows WSL2**
- 安装以下工具：

```bash
sudo apt update && sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso
```

### 编译与运行

```bash
# 克隆仓库
git clone https://github.com/你的用户名/YuanCore.git
cd YuanCore

# 编译并运行
make run
```

### 常用命令

| 命令 | 说明 |
| :--- | :--- |
| `make` | 编译生成 `myos.iso` |
| `make clean` | 清理所有编译产物 |
| `make run` | 在 QEMU 中运行内核 |
| `make clean && make run` | 完全重新构建并运行 |

---

## 📸 运行截图

[运行效果](https://private-user-images.githubusercontent.com/188480328/639501636-bd025583-0aa3-4caa-b5e8-255f434a3333.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODczMjEyNDQsIm5iZiI6MTc4NzMyMDk0NCwicGF0aCI6Ii8xODg0ODAzMjgvNjM5NTAxNjM2LWJkMDI1NTgzLTBhYTMtNGNhYS1iNWU4LTI1NWY0MzRhMzMzMy5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjYwODIxJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI2MDgyMVQxNDAyMjRaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT04ZDljZmE4ZmE1OGVjNTVjMDMxZjUzNTQ0NTRmOGZkYWE2NTQ5YjIxM2NkZGI2MmI2ZWU1ZDViZjZmMjk4YWIwJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZyZXNwb25zZS1jb250ZW50LXR5cGU9aW1hZ2UlMkZwbmcifQ.gy3E6WgqwUPJYFo60AyTu_PsAqF0saEp5OkpOvIVHL8)

---

## ⚠️ 已知限制

- 仅支持 i386 (32位) BIOS 引导
- 暂不支持 x86_64 长模式
- 无中断处理（键盘/鼠标无响应）
- 无内存管理（未启用分页）
- 无文件系统支持

---

## 🔜 后续计划

- [x] 添加键盘中断响应
- [x] 中断体系（IDT / 8259 PIC / PIT 时钟）
- [x] 物理内存管理（帧位图）与内核堆
- [x] 图形模式、桌面 UI 与交互式 shell
- [x] 文件系统（内存版）
- [x] 鼠标驱动与指针交互
- [x] 分页 / ring3 用户态 / int 0x80 系统调用
- [ ] 多任务调度（当前单任务同步执行）
- [ ] 磁盘驱动 + 可持久化文件系统
- [ ] 用户态库（libc 雏形）
- [ ] 支持 x86_64 长模式
- [ ] UEFI 引导支持（通过 Limine）

---

## 📜 许可证

本项目使用 **GPL-3.0** 许可证开源。任何基于本项目的衍生作品也必须在相同的许可证下开源。

---

## 👨‍💻 作者

**bamcmic** — 操作系统爱好者

---

## 🙏 致谢

- [OSDev Wiki](https://wiki.osdev.org/) — 操作系统开发权威参考资料
- [GRUB Manual](https://www.gnu.org/software/grub/) — Multiboot 标准文档
- [QEMU Documentation](https://www.qemu.org/docs/master/) — 虚拟机调试工具

---

**Happy Hacking!** 🚀