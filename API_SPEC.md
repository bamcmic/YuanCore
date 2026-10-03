# YuanCore 程序 API 与加载规范（API v2 / ABI 1.0）

> 本文档是 YuanCore 用户程序体系的**对外契约**，供 YuanCore IDE 与
> 所有应用开发者使用。调用号与内存布局一经发布不再变动，只能追加。

## 1. 可执行格式

| 项 | i386 内核 | x86-64 内核 |
|---|---|---|
| 文件格式 | ELF32 ET_EXEC | ELF64 ET_EXEC |
| e_machine | 3 (EM_386) | 62 (EM_X86_64) |
| 链接基址 | 0x400000 | 0x400000 |
| 入口符号 | `app_main` | `app_main` |
| 建议后缀 | `.app` | `.app`（加载器按 e_machine 自动分派）|
| 大小上限 | 256KB | 256KB |

加载器（`mode/exec.c` / `x64/exec64.c`）解析 program header，
把 PT_LOAD 段拷入目标地址并清零 .bss，然后跳到 `e_entry`。
**禁止**出现基址区间 [0x400000, 0x440000) 之外的 PT_LOAD 段。

## 1.5 分发格式：YCP1 应用包（x86-64 内核）

裸 `.app` 不再直接分发给用户。x86-64 内核的应用以 **YCP1 包**（`.ycp`）分发：

- **容器**：zip，仅 stored 存储（不压缩）
- **条目**：`ycmanifest.json` + `main/` 目录（ELF64 可执行放 `main/` 内）
- **manifest 必需字段**（6 个，缺任一安装失败，`mode/ycp.h` 错误码 -1~-6）：

```json
{
  "name": "counter",
  "version": "1.0",
  "main_dir": "main",
  "entry": "counter.app",
  "author": "...",
  "description": "..."
}
```

- **安装**：`ycp install <path>` 解包到 `/apps/<name>/`；
  **列表**：`ycp list` / `apps`；**卸载**：`ycp remove <name>`
- **启动**：系统扫描 `/apps/*/ycmanifest.json` 动态注册（无需改内核代码），
  启动时按 `main_dir` + `entry` 拼出 ELF 路径经 exec_run 加载
- **ISO 预置包**：Multiboot2 无法传递 GRUB 模块，随 ISO 分发的包在内核
  编译期内嵌为 blob（`mode/*_ycp_blob.h`，`make yapps` 生成），开机写入
  文件系统后与外部安装的包走同一条安装路径

打包工具：`tools/make_ycp.py`；参考样例：`apps/counter.ycp`。
注意：`description` 内禁用双引号与反斜杠（内核侧简易解析，不做转义）。

## 2. 内存布局（两架构一致，物理=虚拟恒等映射）

```
0x00300000  系统调用表（32 × 8 字节，只读约定）
0x00400000  程序加载基址（代码+数据+堆），上限 256KB
0x00500000  (i386) ring3 跳板；(x64) 未使用
0x00800000  用户栈顶，向下生长
```

隔离模型：程序与内核共享恒等映射地址空间（无独立页表），
坏程序会被自己的页错误杀死——这是 YuanCore 的已知设计取舍。

## 3. 系统调用 ABI

### i386
```
int 0x80
eax = 调用号
ebx / ecx / edx / esi / edi = 参数 1..5
参数 6（仅 yc_button 的 active）压在用户栈顶
返回值 = eax
```

### x86-64（API v2 新增，SysV 风格）
```
int 0x80
rax = 调用号
rdi / rsi / rdx / r10 / r8 / r9 = 参数 1..6（全部 64 位传参）
返回值 = rax（32 位有效）
```
apps/ycapi.h 按 `__x86_64__` 宏自动选择调用方式，
**IDE 不需要为架构区分头文件**，换 target 即换 ABI。

## 4. 调用号表（ABI 1.0）

| # | 名称 | 签名 |
|---|---|---|
| 0 | YC_PRINT | void yc_print(const char *s) |
| 1 | YC_PUTC | void yc_putc(char c) |
| 2 | YC_CLEAR | void yc_clear(void) |
| 3 | YC_GETKEY | int yc_getkey(void)（阻塞） |
| 4 | YC_KEY_POLL | int yc_key_poll(void)（-1 无键） |
| 5 | YC_DRAW_WINDOW | void yc_draw_window(x, y, w, h, title) |
| 6 | YC_LABEL | void yc_label(x, y, text) |
| 7 | YC_BUTTON | void yc_button(x, y, w, h, text, active) |
| 8 | YC_FILLRECT | void yc_fillrect(x, y, w, h, 0x00RRGGBB) |
| 9 | YC_REFRESH | void yc_refresh(void) |
| 10 | YC_READ_FILE | int yc_read_file(path, buf, max) |
| 11 | YC_WRITE_FILE | int yc_write_file(path, data, len) |
| 12 | YC_MOUSE_POLL | int yc_mouse_poll(&dx, &dy, &btn) |
| 13 | YC_CURSOR | void yc_cursor(&x, &y) |
| 14 | YC_WAIT_EVENT | int yc_wait_event(&key, &dx, &dy, &btn) |
| 15 | YC_PRINT_DEC | void yc_print_dec(int v) |
| 16/17 | （保留，勿用） | |
| 18 | YC_EXIT | void yc_exit(int code)（不返回） |
| 19 | YC_CLOSE_ALL | void yc_close_all(void) |
| 20 | YC_RANDOM | unsigned yc_random(void) |
| 21 | YC_TICKS | unsigned yc_ticks(void)（PIT 100Hz） |
| 22 | YC_SLEEP_TICKS | void yc_sleep_ticks(n) |
| 23 | YC_PUTPIXEL | void yc_putpixel(x, y, 0x00RRGGBB) |
| 24 | YC_REG_COUNT | int yc_app_count(void) |
| 25 | YC_REG_NAME | void yc_app_name(idx, buf, max) |
| 26 | YC_REG_DESC | void yc_app_desc(idx, buf, max) |
| 27 | YC_TERM_SHOW | void yc_term_show(int on) |
| **28** | **YC_DIR_LIST**（v2） | int yc_dir_list(dir, idx, buf, max)：返回 1=目录 0=文件 -1=结束，名字拷入 buf |
| **29** | **YC_DELETE**（v2） | int yc_delete(path)：递归删除 |
| **30** | **YC_ABI**（v2） | int yc_abi(void)：返回 (主版本<<8)\|次版本，当前 0x0100 |

## 5. 构建命令

### x86-64 程序（YuanCore x64 内核）
```bash
gcc -m64 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c your_app.c -o your_app.o
ld -m elf_x86_64 -e app_main -Ttext-segment=0x400000 \
    -o your_app.app your_app.o
```

### i386 程序（YuanCore i386 内核）
```bash
gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 \
    -mno-sse -mno-sse2 -mno-mmx -msoft-float \
    -c your_app.c -o your_app.o
ld -m elf_i386 -e app_main -Ttext-segment=0x400000 \
    -o your_app.app your_app.o
```

（zig cc 等价命令见 apps/ycapi.h 头部注释。**-mno-sse 系必带**：
QEMU 默认 CPU 无 SSE2，用户态第一条 SSE 指令就是 #UD。）

## 6. 程序装载与注册（YCP1 流程）

1. **安装**：`ycp install <path>`（或内核开机写入内嵌 blob 包）——
   校验 manifest → 整树解压到 `/apps/<name>/`，重复安装覆盖。
2. **注册**：`registry_rescan()` 扫描 `/apps/*/ycmanifest.json`，
   每个包自动出现在开始菜单检索与 `apps` 命令中——无需改内核代码。
3. **运行**：文件管理器点击 / 开始菜单启动 / `run <程序>`。x86-64 内核
   exec 闸门默认启用（`settings exp exec` 可切）；启动路径 =
   `/apps/<name>/<main_dir>/<entry>`。
4. **退出**：程序必须调用 `yc_exit(code)` 返回内核，
   直接 ret/越界会在下一次中断后行为未定义。

## 7. 稳定性约定（对 IDE 的要求）

- 入口函数 `void app_main(void)`，由 yc_exit 结束
- 不使用 libc / SSE / 浮点（-msoft-float 生效）
- 栈由内核提供（约 64KB 空间），避免巨型局部数组
- 打字机式 UI：直接调 yc_* 绘制原语，无 retained 模式
