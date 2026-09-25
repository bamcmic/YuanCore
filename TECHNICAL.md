# YuanCore 技术文档

> 版本 0.2 · 32 位 x86 内核 · GRUB Multiboot 引导

本文面向想读懂/改这份内核的人，按"从按下电源到看到 shell"的顺序讲清每一层。
配套的 API 说明直接写在各头文件的注释里，此处只讲设计与取舍。

---

## 1. 总体架构

```
        GRUB (Multiboot)
              |  eax=0x2BADB002, ebx=&mbi
              v
        boot.asm ----------> 设置栈, 跳入 C
              |
              v
        kernel.c kmain()
              |
   +----------+-----------+--------------+----------------+
   |          |           |              |                |
 gdt.c     idt.c       irq.c         pmm.c           fb.c
 (段)   (异常/向量表) (8259 PIC)  (物理帧位图)     (帧缓冲驱动)
              |           |              |                |
              |         timer.c       heap.c          font.c / console.c
              |        (PIT 100Hz)   (kmalloc)        (点阵字体 / 滚动控制台)
              |           |                             desktop.c (桌面/窗口/任务栏)
              |         kbd.c                             |
              |       (PS/2 键盘)                          v
              +--------------------> shell.c  <---- 图形终端窗口
                                    (命令解释器)
```

分层原则：**底层不认识上层**。`fb.c` 不知道有桌面，`console.c` 不知道有 shell，
`shell.c` 只依赖 console/kbd/timer/pmm/heap 的接口。加新功能从上往下挂，
不要让 `fb.c` 反过来 import 业务逻辑。

```
kernel.c (入口/初始化)
   |
   +-- mode/    内核本体：gdt idt irq timer kbd mouse pmm heap fs exec
   |            fb font desktop theme print
   |
   +-- shell/   命令行与程序：shell.c console.c ui.c syscall.c
                cmd_settings.c cmd_fs.c cmd_mouse.c cmd_exec.c app_settings.c
                （想扩展命令或写带界面的程序，往这个文件夹加文件）
   |
   +-- apps/    用户程序：hello.c（+ ycapi.h 程序侧 API）
```

`mode/` 与 `shell/` 的边界：**shell/ 可以 include mode/，mode/ 绝不 include shell/**。
`theme.c` 放在 mode/ 是因为 desktop 和 console 都要用它，它是 UI 基础设施，
不是某条命令的私有逻辑。

---

## 2. 启动流程

### 2.1 GRUB 阶段

`boot.asm` 在 `.multiboot` 段放了一个 12 个 dword 的 Multiboot 1 头部：

| 偏移 | 内容 | 值 |
|---|---|---|
| 0x00 | magic | `0x1BADB002` |
| 0x04 | flags | `0x7`（bit0 页对齐 / bit1 内存信息 / **bit2 请求图形模式**） |
| 0x08 | checksum | `-(magic+flags)` |
| 0x0C–0x1F | a.out 五字段 | **全 0 占位** |
| 0x20 | mode_type | 0 = 线性帧缓冲 |
| 0x24–0x2C | width/height/depth | 1024 / 768 / 32 |

**为什么有 20 字节占位**：Multiboot 规范说 a.out 五字段只在 bit16 置位时才存在，
但 GRUB2 是按固定偏移读整个 48 字节结构体，`mode_type` 恒从 `+0x20` 取。
不补齐这 20 字节，GRUB 就会把紧跟其后的内核代码当 `mode_type` 读走
（实测读到的是 `kmain` 的函数序言 `53 83 EC 08`，于是报
`unsupported graphical mode type 149717843`）。
bit16 未置位，GRUB 走 ELF 加载路径，这 5 个占位不会被使用。

GRUB 依据请求设置好 1024×768×32 线性帧缓冲，把帧缓冲地址/间距/分辨率
写进 multiboot info 的 `framebuffer_*` 字段（偏移 0x58 起），然后跳到 `start`。

### 2.2 boot.asm

1. `mov esp, stack_top` —— 16KB 栈放在 `.bss` 末尾
2. 按 cdecl 从右往左压栈：`push ebx`（mbi 指针）、`push eax`（magic）
3. `call kmain` —— 从此进入 C
4. 若 kmain 返回则 `cli; hlt` 死循环

### 2.3 kmain 初始化顺序（顺序不能乱）

```
cli()                 <- IDT 就绪前绝不能开中断，否则任何一个 IRQ 都会三重故障
gdt_init()            <- 换掉 GRUB 的描述符表
idt_init()            <- 256 项 IDT + 48 个中断桩
irq_init()            <- 重映射 PIC 到 32-47，只放行 IRQ0/1/2
pmm_init(mbi)         <- 解析内存映射，建帧位图，圈出内核堆
heap_init()           <- kmalloc/kfree 就绪
kbd_init()            <- 注册 IRQ1
timer_init(100)       <- 注册 IRQ0，PIT 100Hz
fb_init(mbi)          <- 校验并接管帧缓冲（失败则回退 VGA 文本模式）
desktop_draw(mbi)     <- 画背景/图标/任务栏/终端窗口外框
console_init(...)     <- 图形控制台绑定到终端窗口
sti()
shell_run()           <- 不返回
```

---

## 3. 内存布局

```
0x00000000 +----------------+
           | 实模式 IVT/BDA/EBDA |  <- PMM 标记为保留
0x000A0000 +----------------+
           | VGA 显存/BIOS ROM |
0x00100000 +----------------+  <- 内核加载基址（链接脚本 . = 1M）
           | .text  (.multiboot 在最前) |
           | .rodata                    |
           | .data                      |
           | .bss   (含栈/页表/TSS)      |
           +----------------------------+  <- kernel_end (linker 导出)
           | PMM 帧位图                  |
0x00300000 +----------------+
           | 系统调用表(1页)              |  <- 程序经 ycapi.h 调用内核
0x00400000 +----------------+
           | 用户程序区(user 页)          |  <- ELF 加载于此, 入口 app_main
           | ... 用户栈顶 0x00800000      |
0x00800000 +----------------+
           | 内核堆 (4MB)                |
           +----------------------------+
           |  <- pmm_alloc_frame() 从这里往后发帧
0xFC000000 +----------------+
           | 帧缓冲窗口(QEMU 在 0xFD000000) |
           +----------------+
```

要点：

- **帧缓冲地址不是固定的**（这次是 `0xFD000000`，PCI 重分配后可能变化），
  必须从 multiboot info 读，绝不能写死。`fb.c` 用 `uintptr_t` 做地址转换，
  因为宿主侧离线预览是 64 位（Windows 是 LLP64，`long` 只有 32 位，会截断地址）。
- `mode/multiboot.h` 用 `_Static_assert` 把 `framebuffer_addr/pitch/bpp/type`
  的偏移钉死在 0x58/0x60/0x6C/0x6D，结构体改坏了编译期就报错，而不是运行期花屏。
- `registers.h` 同样用静态断言钉住中断现场的偏移，和 `isr.asm` 的压栈顺序互为约束。

---

## 4. 中断体系

### 4.1 向量分配

| 向量 | 用途 |
|---|---|
| 0–31 | CPU 异常（除零、缺页、GPF…） |
| 32 (0x20) | IRQ0 — PIT 时钟 |
| 33 (0x21) | IRQ1 — PS/2 键盘 |
| 34 (0x22) | IRQ2 — PIC 级联 |
| 35–47 | 其余硬件中断（当前屏蔽） |

PIC 上电默认把 IRQ0–7 映射到向量 8–15，会跟 CPU 异常撞车，
所以 `irq_init()` 必须先重映射到 32–47。

### 4.2 中断路径

```
硬件 -> CPU -> isr.asm 桩(压 int_no/err_code) -> isr_common(存全部寄存器)
     -> isr_dispatch() / irq_dispatch()   <- C，查表调回调
     <- 恢复寄存器 -> iretd
```

- 从片（IRQ8-15）的中断要给**两片 PIC 都发 EOI**，漏发从片会卡死后续中断。
- 异常默认走 panic：关中断、VGA 文本模式红底白字打印现场（eip/cs/eflags/错误码）后停机。
  此时图形控制台未必可用，所以 panic 走最朴素的文本路径。

### 4.3 关中断的纪律

`pmm.c` 的位图操作、`heap.c` 的 kmalloc/kfree 都可能在时钟中断里被重入，
临界区用 `pushfl; cli ... 按需 sti` 保护。
新增会碰共享状态的代码时，同样要包一层。

---

## 5. 内存管理

### 5.1 物理帧分配器（pmm.c）

- 粒度 4KB，**位图**管理，上限 512MB（位图仅 16KB，够这个阶段用）。
- 可用范围来自 GRUB 的 multiboot memory map（`type == 1` 的段）；
  没有 mmap 时退化为 `1MB + mem_upper`。
- 初始化顺序：位图紧跟 `kernel_end` 放置 → 先全部置占用 → 按 mmap 放行可用段
  → 再把 `[0,1MB)`、内核镜像、位图本身、内核堆整段划掉。
- `pmm_alloc_frame()` 首次适配，`first_free` 缓存上次位置避免每次从头扫。

### 5.2 内核堆（heap.c）

- 空闲链表 + 首次适配，块头 16 字节（size/used/next/prev）。
- 空闲块相邻即合并（前后都并），防止反复分配把堆切碎。
- 剩余空间够大（≥ 32 字节）才切分，否则整块给出，避免产生大量碎块。
- 堆区间由 `pmm_init()` 划出并通过 `pmm_heap_region()` 交给 `heap_init()`，
  两边不会打架。

**尚无分页**。当前内核与用户空间同处一个平坦地址空间，这是下一个大项。

---

## 6. 图形子系统

| 模块 | 职责 |
|---|---|
| `fb.c` | 帧缓冲驱动：`fb_init` 校验 RGB 类型与 32/24/16bpp；提供像素/水平垂直线/矩形填充描边/垂直渐变。颜色统一 `0x00RRGGBB`，由驱动按 bpp 换算 |
| `font.c` + `font_data.h` | 8×16 点阵字模（Consolas 13 光栅化，95 个可打印 ASCII），`scale` 参数可 2x 放大。**字模统一基线绘制**，保证 `g`/`y` 的下伸部正确 |
| `console.c` | 图形模式下的滚动控制台：自维护字符缓冲（8×18 单元），滚动整块重绘，支持 `\n \r \t \b` |
| `desktop.c` | 桌面外观：渐变背景、左侧图标、窗口（标题栏 + × 关闭钮）、底部任务栏 |

**UI 标准**（新增界面请沿用）：

- **所有颜色都从 `mode/theme.c` 取**（`theme_get(THEME_XXX)`），UI 代码里不要硬编码颜色。
  这是设置功能能即时换肤的前提。
- 窗口 = 阴影(偏移 5px) + 1px 边框 + 36px 标题栏 + 客户区；标题栏文字 scale=2。
- 任务栏高 36px，左侧 Start 按钮（`THEME_ACCENT`），右侧灰字状态信息。
- 终端窗口的几何常量在 `desktop.h`（`SHELL_WIN_*`），`console_init` 用同一组值对齐。

### 主题与设置

`theme.c` 维护一张 18 项的颜色表，外加 5 套预置：

```
default / ocean / sunset / matrix / light
```

改色后调用 `shell_redraw_ui()`（内部 = `desktop_draw(mbi)` + `console_redraw()`）即可整屏换肤，
历史输出也会跟着变色（console 只存字符不存颜色，重绘时统一取当前前景色）。

要新增一个可配置颜色：`theme.h` 的 enum 补一项 → `theme.c` 的
`defaults[]`、各 preset 数组、`key_names[]` 各补一列，三处对齐即可。

**离线预览**：`mode/desktop.c`、`fb.c`、`font.c`、`theme.c`、`shell/console.c`
不依赖任何内核设施，可以在宿主机上直接链接成预览程序（见仓库外的 `preview/main.c`），
伪造 `multiboot_info` 渲染成 PNG，还能指定预置主题看换肤效果。改 UI 不用每次进 QEMU。

---

## 5.5 鼠标（mode/mouse.c）

PS/2 鼠标走 8042 的 aux 口、IRQ12。初始化顺序：
开 aux 口(0xA8) → 改控制器配置(置 IRQ12 位、放行鼠标时钟) → 默认设置(0xF6) →
开数据上报(0xF4)。数据包 3 字节，第一字节 bit3 恒 1 用于包同步；
dx/dy 是 9 位有符号数（溢出位在第一字节的 bit4/bit5）。

UI 集成（shell/ui.c）：

- **光标**：9×14 箭头，白色 + 黑色投影。移动时先把上次保存的背景块写回，再保存新位置、画箭头。
  光标永远最后画（`ui_draw()` 末尾），保证盖在所有窗口之上。
- **点击**：`ui_button()` 绘制时登记矩形；左键**按下沿**做命中测试，
  命中则聚焦该按钮并直接派发 `KEY_ENTER`——现有程序不用改一行就获得鼠标支持。
- shell 主循环改为轮询（键盘 + 鼠标），都没事件才 `hlt`。

`mouse` 命令可实时观察事件流，Esc 退出。

---

## 5.8 程序加载执行（mode/exec.c）

"程序"就是一个**标准 ELF32 可执行文件**，放在文件系统里，`run <path>` 启动。

- 链接基址固定 4MB（`MEM_EXEC_BASE`），入口符号 `app_main`
- 加载器：校验 ELF 头 → 遍历 program header，把 PT_LOAD 段拷到对应虚拟地址、
  `p_memsz > p_filesz` 的部分清零(.bss) → 跳到 `e_entry`
- **系统调用表**：固定地址 3MB（`MEM_SYSCALL_BASE`），程序按 `apps/ycapi.h`
  里约定的下标取函数指针调用内核服务（print / 窗口 / 按钮 / 按键 / 文件 / 鼠标…共 18 个）。
  下标已发布，只能追加不能改动。

内存布局新增（见 mode/memmap.h）：

```
0x00300000  系统调用表（1 页，PMM 标记保留）
0x00400000  程序加载区（256KB）
0x00800000  内核堆（4MB）—— 原来在位图后面，给程序区让位
```

构建一个程序（WSL）：

```bash
gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -c hello.c -o hello.o
ld  -m elf_i386 -e app_main -Ttext-segment=0x400000 -o hello.app hello.o
```

`make apps` 一键编译示例。内核启动时会把内置的 hello 程序写进
`/apps/hello.app`，所以 `run /apps/hello.app` 开箱即用。

**没有隔离**：程序与内核同特权级、无分页保护，坏程序会带崩系统。
这是当前架构的已知边界，等 ring3 + 分页就绪后升级为真正的系统调用。

---

## 5.9 分页 / ring3 / 系统调用（v0.3 核心）

这是"程序加载执行"的安全底座，三者一起完成：

**分页（mode/paging.c）** —— 单页目录模型（同一时刻只有一个用户程序）：

```
0x00000000-0x003FFFFF  内核镜像/位图/文本显存        supervisor RW
0x00400000-0x007FFFFF  用户程序区 + 用户栈(栈顶8M)    user RW   ← 保护边界
0x00800000-0x00BFFFFF  内核堆                        supervisor RW
0xFC000000-0xFFFFFFFF  帧缓冲窗口(QEMU 在 0xFD000000) supervisor RW
0xFFC00000             递归映射页目录                  supervisor RW
```

user 位就是隔离：ring3 碰任何内核页 → 页错误。页表本体是内核 BSS 的静态数组
（低 4M 内天然已映射，避开"建表要用表"的死锁）。

**ring3（mode/tss.c + mode/switch.asm）**：

- TSS 只用于一件事：ring3 发生中断时 CPU 能切到内核栈（ss0/esp0 指向 16KB 专用栈）。
- `user_enter(entry, ustack)`：保存内核现场 → 在用户栈顶放 4 个数据段选择子 →
  iret 进放在用户区的 **8 字节跳板**（`pop ds/es/fs/gs; jmp eax`）。
  必须有跳板，因为 iret 进 ring3 后段寄存器还是 ring0 的 0x10，直接用就 GPF。
- `user_exit()`：程序调 YC_EXIT 时恢复内核现场返回，exec_run 拿到控制权继续。

**系统调用（int 0x80）**：IDT 门 DPL=3（flags 0xEE）。
eax=号，ebx/ecx/edx/esi/edi=前 5 个参数，第 6 个参数压在用户栈顶
（内核从中断帧里的 user_esp 取）。返回值写 eax。YC_EXIT 触发 user_exit。

**用户态异常**：GPF/页错误若来自 ring3（cs&3≠0）→ 打印现场并杀掉程序回 shell，
不再整个系统停机。

syscall 一览（apps/ycapi.h，**下标已发布只能追加**）：

| 号 | 名称 | 原型 |
|---|---|---|
| 0-2 | print/putc/clear | 输出到控制台 |
| 3-4 | getkey/key_poll | 阻塞 / 非阻塞取键 |
| 5-7 | draw_window/label/button | 窗口框架 / 文字 / 按钮 |
| 8 | fillrect(x,y,w,h,rgb) | 实心矩形 |
| 9 | refresh | 重绘桌面+终端 |
| 10-11 | read_file/write_file | ramfs 读写 |
| 12-13 | mouse_poll/cursor | 鼠标相对事件 / 光标绝对位置 |
| 14 | wait_event | 阻塞等键盘或鼠标事件 |
| 15 | print_dec | 十进制输出 |
| 18 | exit(code) | 退回 shell |
| 19 | close_all | 关闭所有 UI 窗口 |
| 20 | random | 32 位 xorshift32（种子=开机 tick） |
| 21 | ticks | PIT tick 数（100Hz） |
| 22 | sleep_ticks(n) | 内核 hlt 等 n 个 tick（程序节拍器，不烧 CPU） |
| 23 | putpixel(x,y,rgb) | 描点 |
| 24 | reg_count | 应用注册表条目数 |
| 25 | reg_name(idx, buf, max) | 拷出应用名（内核字符串不能直接给 ring3 指针） |
| 26 | reg_desc(idx, buf, max) | 拷出应用描述 |
| 27 | term_show(on) | 显隐 YuanCore Shell 终端窗口（on=0 关，1 开） |

写一个用户程序的模式：

```c
#include "ycapi.h"
void app_main(void) {
    for (;;) {
        draw();                                    /* 自己画界面 */
        int t = yc_wait_event(&k, &dx, &dy, &b);   /* 阻塞等事件 */
        if (t == 2) { /* 鼠标：yc_cursor 取绝对位置做命中 */ }
        else { /* 键盘 */ }
        if (done) break;
    }
    yc_exit(0);
}
```

节奏型程序（游戏）用 `yc_sleep_ticks(n)` 做主循环节拍：

```c
while (!over) {
    yc_sleep_ticks(ticks_per_frame);   /* 内核里 hlt，CPU 不空转 */
    while ((k = yc_key_poll()) != -1) handle(k);
    step(); draw();
}
```

内置程序（开机自动写进 /apps/，源码 apps/，随内核 blob 分发）：

| 文件 | 说明 |
|---|---|
| `/apps/hello.app` | 计数按钮窗口，wait_event 模型范例 |
| `/apps/closeall.app` | 关闭所有 UI 窗口 |
| `/apps/snake.app` | **贪吃蛇**：方向键、吃食物加速、Enter 重开、Esc 退出，开机内记录最高分 |

改了 apps/ 下的程序要重新生成对应 `mode/app_*_blob.h`
（构建命令见 ycapi.h 顶部，`make apps` 一并构建），内核才能把新版本分发进文件系统。

---

---

## 6.5 文件系统（mode/fs.h）

内存文件系统（ramfs）：**目录树 + 堆上存储**，掉电即失，重启回初始状态。

选型理由：零依赖、代码量小，足以演示完整的 VFS 语义（路径解析、创建/读写/
删除/遍历）。以后接真实磁盘时，把这层换成块设备后端，接口不变。

- 条目上限 96，单文件上限 4096 字节，名字最长 30 字符
- 路径必须是**绝对路径**（以 `/` 开头），不支持 `.` / `..` 与相对路径
- `fs_write` 对不存在的路径会**自动建文件**；目录非空不能删
- `fs_read/write/append` 的内存在堆上分配，删除时释放

命令：`ls`、`cat`、`write <file> <text>`、`mkdir`、`rm`、`df`

### 主题持久化

`theme_save()` 把 18 个颜色项写成 `/settings.cfg`（每行 `key=RRGGBB`），
`settings set/preset/reset` 改完自动保存；`theme_init()` 启动时若该文件存在则加载。
注意：宿主是内存文件系统，**重启后配置仍会回到默认**——
等块设备驱动就绪后，把 cfg 文件放到磁盘分区即可获得真正的持久化。

---

## 6.8 性能优化

| 项 | 做法 | 效果 |
|---|---|---|
| 32bpp 填充/画线 | `fb_hline` / `fb_fillrect` 对 32bpp 直写显存，绕过逐像素的 `fb_putpixel` | 全屏清屏/渐变从 ~78 万次函数调用降为 ~78 万次内存写 |
| 渐变插值 | 原来两项各自整除再相加，结果抖动 ±1 出现非单调条纹；改为**先加权求和再一次取整** | 条纹消除（用预览器逐行断言验证为 0） |
| UI 重绘 | `ui.c` 引入脏标记：按键没引起变化就不重绘 | 无效按键零开销 |

调这类优化时，预览器是最好的回归工具——改完直接逐像素断言，不用进 QEMU。

---

## 7. Shell

命令是**注册制**的：`shell_register_command()` 公开，内置命令在
`register_builtin_commands()` 里登记，独立命令模块在自己的 `xxx_init()` 里登记。

| 命令 | 作用 |
|---|---|
| `help` | 列出所有已注册命令（自动包含后加的） |
| `clear` | 清空控制台 |
| `about` | 内核与显示信息 |
| `mem` | 物理内存总量/占用/空闲 + 堆统计（含 kmalloc/kfree 自检） |
| `uptime` | 开机秒数 |
| `ticks` | 时钟原始 tick 数 |
| `date` | CMOS 实时时钟（RTC 不可用时报 uptime） |
| `echo <文本>` | 原样输出 |
| `settings` | UI/色彩设置（见下） |
| `ls` / `cat` / `write` / `mkdir` / `rm` / `df` | 文件操作（见 6.5 节） |
| `reboot` | 通过 8042 键盘控制器复位 |

### settings 命令

```
settings                       列出所有颜色项与当前值(RRGGBB)
settings set <key> <RRGGBB>    改单项，如 settings set titlebar 2E6CB0
settings preset <name>         应用预置主题
settings presets               列出可用预置主题
settings wallpaper <style>     壁纸样式：gradient / solid / dots
settings clock on|off          任务栏时钟开关
settings reset                 恢复 default 预置
```

可配置项（18 个颜色）：`bg_top` `bg_bottom` `taskbar` `taskbar_line` `titlebar`
`title_text` `window_bg` `window_border` `text` `text_dim` `accent` `danger`
`console_fg` `console_bg` `icon_files` `icon_terminal` `icon_settings` `icon_about`；
另有非颜色选项 `bg_style`（壁纸）与 `clock`（时钟开关），一并持久化。

改完立即生效（重绘 + 自动保存到 `/settings.cfg`）。宿主是 ramfs，
**重启后配置仍回默认**——真持久化要等块设备驱动把 ramfs 换成磁盘后端。

### 鼠标点击与桌面

`ui_mouse()` 的左键按下沿按三段路由：

1. **有窗口打开**：先查窗口右上角 × （`desktop_close_hit`）→ 关闭；
   再查窗口绘制时登记的按钮矩形 → 聚焦并触发。
2. **无窗口**：查桌面图标（`desktop_icon_hit`，命中区含标签）→ 经
   `ui_set_launch()` 注册的回调打开对应程序（Files 浏览器 / 回终端 /
   Settings 窗口 / About 窗口）；再查任务栏 Start → Start 菜单。

命中区几何与绘制共用同一组常量（`desktop.c`），改布局不会出现"画的对不上的
点不着"。任务栏时钟由 shell 主循环每秒调 `ui_clock_tick()` 刷新，
文本来自 RTC（`mode/rtc.c`，CMOS 0x70/0x71，UIP 等待 + 双读校验），
RTC 异常时退回开机时长。

### 屏幕合成器（v0.5）

屏幕按 z 序分四层，每层都能**按矩形局部重画**：

```
第 4 层  光标            ui.c（保存/恢复块）
第 3 层  模态窗口        ui.c（ui_open 打开的）
第 2 层  控制台内容      console.c → console_repaint_region(x,y,w,h)
第 1 层  桌面            desktop.c → desktop_repaint_region(x,y,w,h)
                         （壁纸渐变逐行/图标/终端窗口框/任务栏，各自按相交矩形重画）
```

`ui.c` 的 `compose_region(x,y,w,h)` 把一个矩形从第 1、2 层重新合成出来。
**光标移动（无窗口时）= 对旧光标矩形做一次 compose** —— 从内容源头重建，
没有任何"截屏备份"，从构造上杜绝残影（拖影黑线的三次教训都在这一层终结）。

- 有模态窗口时窗口内容静止，光标仍走保存/恢复块（快且准确）；
  注意保存块是 `(CUR_W+1)x(CUR_H+1)`——**必须比箭头大一圈**，
  因为黑色投影偏移 +1px 画在箭头矩形之外。
- `desktop_repaint_region` 重画壁纸时逐行重算渐变插值（先加权再一次取整，
  见 6.8 节的整除教训），点阵壁纸重铺相交区域的点。
- 时钟每秒刷新只重画任务栏 96px 宽的小区域。

### 如何新增一条命令

1. 在 `shell/` 下新建 `cmd_xxx.c`；
2. 实现 `void cmd_xxx(const char *args)` 和 `void cmd_xxx_init(void)`
   （init 里调 `shell_register_command()`）；
3. 在 `shell/shell.c` 的 `shell_init()` 里补一行 `cmd_xxx_init();`。

`cmd_settings.c` 就是现成的范例，照抄结构即可。

键盘：Scan Code Set 1。Shift 大小写、退格、回车之外，还解码 0xE0 扩展键
（方向键 / Delete / Home / End / PgUp / PgDn，返回 `KEY_*` 常量），
并把 Ctrl+字母 转成标准控制码（Ctrl+C=0x03、Ctrl+L=0x0C、Ctrl+U=0x15）。
驱动内部是环形缓冲，`kbd_getchar()` 无按键时 `hlt` 等中断，不烧 CPU。

### 行编辑与快捷键

| 按键 | 行为 |
|---|---|
| 左/右方向键 | 移动光标（支持在行中间插入/删除） |
| Backspace / Delete | 删光标前 / 后一个字符 |
| Home / End | 跳到行首 / 行尾 |
| Up / Down | 翻命令历史（最多 16 条，去重） |
| Tab | 命令名补全：唯一候选直接补全，多个候选补到公共前缀并列出 |
| Ctrl+C | 放弃当前行，打印 `^C` |
| Ctrl+L | 清屏并重画当前行 |
| Ctrl+U / Esc | 清空当前行 |

实现要点：console 提供了 `console_cursor_get/set`、`console_clear_to_eol`、
`console_cols`，shell 记住提示符起点，每次编辑后重画整行再把光标放回编辑位置。
输入长度上限是 `console_cols() - 提示符宽度`，保证单行内编辑不涉及折行。

---

## 7.5 窗口 UI 工具包（shell/ui.h）

给其它"程序"用的极简窗口框架。**模态**：有窗口打开时 shell 不收字符，
按键全部转给最上层窗口；Esc 关闭。没有鼠标，导航靠 Tab/方向键 + Enter。

```c
static void on_draw(int win) {
    int x, y, w, h;
    ui_client(win, &x, &y, &w, &h);      /* 客户区（去掉标题栏和内边距） */
    ui_focus_count(2);                   /* 声明有几个可聚焦项 */
    ui_label(x, y, "hello");
    ui_button(x, y + 24, w - 20, 30, "OK", 0);      /* idx==焦点时高亮 */
    ui_button(x, y + 64, w - 20, 30, "Close", 1);
}
static void on_key(int win, int key) {
    switch (key) {
    case KEY_UP: case KEY_LEFT:   ui_focus_prev(); break;
    case KEY_DOWN: case KEY_RIGHT: ui_focus_next(); break;
    case KEY_ENTER:               if (ui_focus() == 1) ui_close(win); break;
    }
}
ui_open(320, 130, 380, 260, "My App", on_draw, on_key);
```

要点：

- 焦点存在窗口里，`ui_focus()` 返回当前索引；`ui_button(..., idx)` 自动画高亮框。
- `ui_key()` 集中处理 Esc（关闭）和 Tab（切焦点），其余按键透传给 `on_key`。
- 每次按键后整体重绘：先恢复桌面+终端，再画窗口。正确性优先，
  全屏渐变约 78 万次描点，QEMU 下几十毫秒；以后可加脏矩形优化。
- `app_settings.c` 是完整范例：`settings ui` 打开主题选择窗口，
  方向键 + Enter 即可换肤，右侧三个色样实时反映当前主题的 titlebar/accent/console bg。

新增一个带界面的程序 = 在 `shell/` 加一个 `app_xxx.c` +
写 `app_xxx_open()` + 在某条命令里调用它。

---

## 8. 构建与运行

```bash
# 依赖（Ubuntu / WSL2）
sudo apt update && sudo apt install -y \
    build-essential gcc-multilib nasm qemu-system-x86 grub-pc-bin xorriso

make          # 生成 YuanCore.iso
make run      # QEMU 弹窗运行（已内置 WSLg 兼容的环境变量兜底）
make shot     # 无头截图：不出窗口也能拿到整屏 PPM
make debug    # QEMU -s -S，等 gdb 连 :1234
make clean
```

WSLg 相关：`run`/`debug` 会自动带 `DISPLAY=${DISPLAY:-:0}`、`GDK_BACKEND=x11`、
`SDL_VIDEODRIVER=x11`，绕开"任务栏有图标但窗口不渲染"的 Wayland 后端问题。
若窗口仍不出现，`make shot` 拿截图最稳。

### 目录结构

```
myos/
├── boot.asm             # Multiboot 头部 + 入口
├── kernel.c             # 内核入口，初始化顺序的"目录"
├── linker.ld            # 链接脚本（1M 基址，导出 kernel_end）
├── Makefile             # 自动扫描 *.c / mode/*.c / shell/*.c 与 *.asm
├── TECHNICAL.md         # 本文档
├── mode/                # 内核本体
│   ├── multiboot.h      # mbi 结构体 + 编译期偏移断言
│   ├── io.h             # 端口读写
│   ├── registers.h      # 中断现场布局 + 静态断言
│   ├── gdt.c/.h         # 段描述符
│   ├── isr.asm          # 48 个中断桩 + 公共保存/恢复
│   ├── idt.c/.h         # IDT 与异常分发
│   ├── irq.c/.h         # PIC 重映射、EOI、IRQ 注册
│   ├── timer.c/.h       # PIT 时钟
│   ├── kbd.c/.h         # PS/2 键盘
│   ├── mouse.c/.h       # PS/2 鼠标（IRQ12，三字节包）
│   ├── exec.c/.h        # ELF32 程序加载器
│   ├── memmap.h         # 物理内存布局约定（程序区/系统调用页/堆）
│   ├── pmm.c/.h         # 物理帧位图分配器
│   ├── heap.c/.h        # kmalloc/kfree
│   ├── fs.c/.h          # 内存文件系统（目录树 + 堆存储）
│   ├── app_hello_blob.h # 内置演示程序二进制（自动生成）
│   ├── fb.c/.h          # 帧缓冲绘图原语（32bpp 有直写快路径）
│   ├── font.c/.h + font_data.h    # 点阵字体
│   ├── theme.c/.h       # 主题/色彩设置（UI 唯一取色处）
│   ├── desktop.c/.h     # 桌面外观与 UI 标准
│   └── print.c/.h       # VGA 文本模式（回退/panic 用）
└── shell/               # 命令行与程序（扩展往这里加）
    ├── shell.c/.h       # 命令注册、行编辑、历史、快捷键、UI 路由
    ├── console.c/.h     # 图形滚动控制台（含光标控制 API）
    ├── ui.c/.h          # 窗口 UI 工具包（模态、焦点、按钮/标签、鼠标光标）
    ├── syscall.c/.h     # 程序系统调用表（固定地址 3MB）
    ├── app_settings.c   # 图形化主题设置窗口（settings ui 打开，UI 范例）
    ├── cmd_settings.c   # settings 命令（新增命令的范例）
    ├── cmd_fs.c         # ls / cat / write / mkdir / rm / df（命令模块范例）
    ├── cmd_mouse.c      # mouse 命令（实时事件）
    └── cmd_exec.c       # run 命令（启动程序文件）
```

> 注：`mode/shell.*` 与 `mode/console.*` 已迁移到 `shell/`，旧位置留了空壳文件
> （只含一个 typedef，不定义任何符号）。确认无用后可直接删掉这 4 个文件。

---

## 9. 桌面形态与应用模型（v0.6）

### 9.1 屏幕层级与任务栏

    光标 → 模态窗口(可拖动) → 控制台 → 桌面(壁纸/图标/终端框/任务栏)

- **任务栏在右侧竖放**（TASKBAR_W=48）：Start 在条顶，时钟(HH/MM/SS 竖排)在条底。
  收起后右缘只留 4px 高亮提示条。
- **Win 键**（kbd.c 扩展码 E0 5B/5C → KEY_WIN）或点提示条唤起/收起；
  收起时图标、终端窗口都不受影响，窗口可铺满全屏宽。
- 桌面图标 4 个：Files(文件管理器) / Terminal(重开终端) / Settings(主题) / About。

### 9.2 终端窗口生命周期

- YuanCore Shell 窗口可点 × 关闭（desktop_terminal_show(0)）。
- 关闭期间 console 只更新字符缓冲不上屏（console.c 各绘制入口门控），
  重开（Terminal 图标 / 开始菜单 Terminal / syscall 27）即完整恢复，
  历史输出不丢。
- shell 本体在缓冲里照常工作；关闭窗口时打字不可见属预期行为。

### 9.3 窗口拖动

- ui.c 的 ui_mouse：左键在模态窗口标题栏(避开 × 与边框)按下 → 进入拖动，
  移动事件驱动窗口坐标（屏幕内 clamp），每次移动整屏重排（restore_background
  + 全窗口 + 光标）。约 78 万像素写/次，QEMU 下轻微拖影感属正常，脏矩形是后续优化。

### 9.4 应用注册表（shell/registry.c）

- 内置静态表（name/desc/path），新增应用加一行即可。
- 三个消费端：开始菜单检索（大小写不敏感子串过滤）、apps 命令、int 0x80 查询。
- registry_launch 经 shell 注入的启动器回调（= exec_run），registry 不直接依赖 exec。
- 程序侧 API 见 ycapi.h 24-26（字符串拷贝语义，ring3 读不了内核段）。

### 9.5 文件管理器（shell/app_desktop.c）

- 目录导航：方向键 + Enter，或鼠标点行；".." 回上级；目录以 / 后缀显示。
- 文本文件 Enter 进入查看视图（≤600B），Back/Esc 返回列表。
- 行即按钮（ui_button 登记命中区），键盘焦点与鼠标点击走同一条 ENTER 路径。

---

## 9.5 x86-64 / EFI 迁移（进行中）

GRUB 引导保持不变:GRUB 的 EFI 版同样支持 Multiboot2,因此引导器只换固件入口,不换软件。

- **入口**:`x64/boot32.asm` —— Multiboot2 头(含 framebuffer 1024x768x32 请求)+
  32 位入口桩:校验魔数(0x36D76289)→ 构建四级页表(PML4[0]->PDPT[0..3]->PD[0..2047],
  2MB 大页恒等映射低 4GB,覆盖 0xFD000000 帧缓冲)→ PAE + EFER.LME + CR0.PG(+WP)→
  ljmp 进 64 位代码 → 调 `kmain64`。
- **信息适配**:`x64/kernel64.c` 解析 Multiboot2 tag(meminfo/mmap/framebuffer),
  合成既有 multiboot1 风格的 `struct multiboot_info`(静态缓冲)——
  fb/pmm/desktop 等模块零改动复用。
- **C 层双构建**:同一份 C 源同时可编 i386 与 x86-64(`-DYC_X64`);
  差异点用条件编译:GDT/IDT/PIC/分页各有 x64 专用实现
  (`x64/gdt64.c idt64.c paging64.c irq64.c`),ring3(exec/switch/tss)不迁移。
- **构建**:`make x64` 出 `YuanCore64.iso`(grub-mkrescue,含 BIOS+EFI 双启动,
  EFI 需要 `grub-efi-amd64-bin` 与 `mtools`);`make run64` BIOS 路径,
  `make run-efi` 走 OVMF(需 `ovmf` 包,`EFI_D=` 可指定固件目录)。
- **内核编译旗标**:`-mno-red-zone`(中断不经内核栈红区必须关)
  \ (-msoft-float)。
- **未迁移**:ring3 程序执行(exec/switch/usermode)—— 见 10 节已知限制。
- **汇编未离线验证**:boot32/isr64 为 nasm -f elf64,链接期才暴露;C 层已全量零警告。

---

## 10. 已知限制

- **ring3 程序执行默认禁用**（exec.c 稳定性闸门）：切换路径存在两次未定位
  根因的内核态踩踏史（IDT 门被覆盖，已布 CR0.WP 只读陷阱未获回传结果），
  为保证系统可用， 与开始菜单启动项默认拒绝并提示；
  调试用 `settings exp exec on` 启用。修复定位前不作为稳定功能宣传。
- 单用户程序：同一时刻只有一个 ring3 程序（exec 同步执行）。
- 程序与内核的隔离靠分页 user 位；无段级保护、无 Cox。
- ramfs 掉电即失；/settings.cfg、/apps 均为开机重建。
- 无鼠标指针以外的光标文本输入焦点管理；窗口无最小化。
- 仅 i386 BIOS 引导。

## 11. 路线图

1. 脏矩形重绘（拖动窗口/合成器的带宽优化）
2. 多程序并存 + 简单调度器（PIT tick 已就绪）
3. 块设备驱动（ATA PIO）→ ramfs 换磁盘后端，设置/应用真持久化
4. ELF 程序动态注册进 registry（扫描 /apps）
5. x86_64 长模式迁移（Limine + Multiboot2）

---

*文档对应源码版本：0.6。改了架构记得同步这里。*
