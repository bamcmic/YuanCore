/* mode/fsypk.h - YPK 目录打包容器（压缩/解压）
 *
 * 一个 .ypk 文件 = 一棵目录树的快照：
 *   文件头  "YPK1" + u32 LE 原始流长度 + LZSS 压缩数据
 *   原始流  记录序列 [type][nlen][name][size LE][data]
 *           type: 1=目录 2=文件；0xFF = 流结束
 *
 * LZSS: 4096 字节滑窗,匹配长度 3..18,哈希链加速(链长上限 64),
 * 避免 O(n^2) 暴力匹配 —— 256KB 树在 QEMU 里亚秒完成。
 * 解码端窗口初始化为 0,编码端一致,虚拟历史匹配合法。
 */
#ifndef FSYPK_H
#define FSYPK_H

#include "fs.h"

/* 把 dirpath 整棵子树打包成 out_path(一个普通文件)。
 * 成功返回 0;错误码同 fs.h(另有 FS_NOSPACE = 堆/容器超限)。 */
int fs_ypk_pack(const char *dirpath, const char *out_path);

/* 把 ypk_path 解包到 dest_dir 下(目录不存在自动创建;
 * 同名条目覆盖文件、目录跳过)。成功返回 0。 */
int fs_ypk_unpack(const char *ypk_path, const char *dest_dir);

#endif /* FSYPK_H */
