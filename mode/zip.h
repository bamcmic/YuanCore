/* zip.h - 仅 stored 的 zip 只读解析器(YCP1 容器规格)
 *
 * 支持范围(刻意最小化):
 *   - 无压缩(method 0, stored)条目; 压缩条目读入时报 ZIP_E_METHOD
 *   - 标准 EOCD(不含 zip64); 条目数上限 256; 文件上限 4MB
 *   - 中央目录 + local header 双重定位, 数据偏移按 local 头实测
 */
#ifndef ZIP_H
#define ZIP_H

typedef struct {
    char           name[64];   /* 包内路径, 不含前导 '/' */
    unsigned int   size;       /* 未压缩大小 */
    unsigned int   data_off;   /* 数据在文件内的偏移(stored) */
    unsigned int   method;     /* 0 = stored */
} zip_entry;

typedef struct {
    unsigned char *data;       /* 整个 zip 文件(堆上) */
    unsigned int   size;
    int            count;
    zip_entry     *entries;
} zip_file;

/* 错误码: 0 成功; -1 fs/内存; -2 非 zip/结构损坏; -3 超限 */
int  zip_load(const char *path, zip_file *zf);
void zip_free(zip_file *zf);

int  zip_find(const zip_file *zf, const char *name);   /* -> idx 或 -1 */
/* 读 entry 数据(stored); max 不足截断返回 max; 压缩条目返回 ZIP_E_METHOD */
#define ZIP_E_METHOD (-2)
int  zip_read(const zip_file *zf, int idx, unsigned char *buf, int max);

#endif /* ZIP_H */
