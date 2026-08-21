# YuanCore（元核）
是一个从零开始的X86_64(暂时仅支持X86)操作系统

使用GRUB Multiboot 引导，
以c语言和asm汇编编写
# 📖 项目简介
YuanCore 是一个面向所有人的操作系统，旨在提供更现代化的体验和更加便捷的创作体验

## 技术栈
汇编器 NASM \
编译器 GCC(i386-efi)\
引导器 GRUB\
模拟器 QEMU\
构建工具 Make\
链接器 LD

## 目录结构
还没有目录结构(未来可期)

# 构建与使用
任意Linux发行版，推荐使用Ubuntu\
windows 可以使用 wsl2\
### 安装工具
```
sudo apt update && sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso
```
安装完成后，切换到解压后的源码目录\
运行构建
`make run`\
### 效果如下
[qemu输出](https://private-user-images.githubusercontent.com/188480328/639501636-bd025583-0aa3-4caa-b5e8-255f434a3333.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3ODczMjAzMTYsIm5iZiI6MTc4NzMyMDAxNiwicGF0aCI6Ii8xODg0ODAzMjgvNjM5NTAxNjM2LWJkMDI1NTgzLTBhYTMtNGNhYS1iNWU4LTI1NWY0MzRhMzMzMy5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjYwODIxJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI2MDgyMVQxMzQ2NTZaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT05NWU5ZmJjMjg3MjJiNTMxY2Q3OGEyNWM0NDY0NjY2NTM3NjkwZTI4YTBkNzc0YmUxNDJkN2Y1ZjAwZTk2ODc1JlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZyZXNwb25zZS1jb250ZW50LXR5cGU9aW1hZ2UlMkZwbmcifQ.cmqD2Rdm8c9SCsGaNQ1xGZwkNNX7EWHo20beS_P_7Ug)


### 使用
`make clean`\
清除构建内容

# 结语
bamcmic - 操作系统爱好者\
    🙏 致谢
    OSDev Wiki - 操作系统开发权威参考资料\
    GRUB Manual - Multiboot 标准文档\
    QEMU Documentation - 虚拟机调试工具

## 本项目使用
GPL-3.0 license许可证