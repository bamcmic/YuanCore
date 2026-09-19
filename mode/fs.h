/* fs.h - 内存文件系统
 *
 * 目录树 + 堆上存储的 RAM 文件系统（掉电即失，重启回初始状态）。
 * 路径必须是绝对路径、以 '/' 开头，不支持 "." / ".." 与相对路径。
 * 接口返回负值表示错误（见下方错误码）。
 */
#ifndef FS_H
#define FS_H

#define FS_OK      0
#define FS_ERR     -1      /* 通用错误（非法路径/参数） */
#define FS_NOTFOUND -2
#define FS_EXISTS  -3
#define FS_NOSPACE -4      /* 条目或文件大小超限 */
#define FS_NOTEMPTY -5
#define FS_ISDIR   -6

#define FS_NAME_MAX 30
#define FS_MAX_FILE 262144     /* 单文件上限 256KB（要装得下可执行程序） */

int fs_init(void);
int fs_ready(void);

int  fs_exists(const char *path);
int  fs_is_dir(const char *path);
int  fs_size(const char *path);      /* 不存在或目录返回 -1 */

int  fs_create_file(const char *path);
int  fs_create_dir(const char *path);

/* 文件不存在会自动创建；超出 FS_MAX_FILE 返回 FS_NOSPACE */
int  fs_write(const char *path, const void *data, int len);
int  fs_append(const char *path, const void *data, int len);

/* 最多读 max 字节，返回实际读到的字节数；出错返回负值 */
int  fs_read(const char *path, void *buf, int max);

int  fs_delete(const char *path);    /* 目录必须为空 */

/* 遍历目录：idx 从 0 递增，没有下一项时返回 0 */
const char *fs_list(const char *dir, int idx, int *is_dir, int *size);

/* 统计 */
void fs_stats(int *used_entries, int *total_entries, int *bytes_used);

#endif /* FS_H */
