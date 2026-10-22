/* ycp.h - YCP1 应用包: 安装/删除/已装清单 + manifest 迷你 JSON
 *
 * 规格模式 YCP1:
 *   <name>.ycp = zip(仅 stored)
 *     ycmanifest.json          包根固定自述(必需)
 *       必需字段: name, version, main_dir, entry, author, description
 *       可选字段: icon (v1 预留, 不实现)
 *     <main_dir>/              文件夹名由自述的 main_dir 定义
 *       <entry>                ELF64 可执行(入口 app_main, 0x400000)
 * 安装: 校验后整树解压到 /apps/<name>/, 重复安装覆盖。
 * 启动: exec_run("/apps/<name>/<main_dir>/<entry>")。
 * 旧裸 .app 全面替换, 不再兼容。
 */
#ifndef YCP_H
#define YCP_H

/* 错误码: 0 成功
 * -1 zip 加载失败  -2 无 ycmanifest.json  -3 字段缺失/非法
 * -4 内容缺失(entry 落盘失败)  -5 包内路径非法  -6 fs 错误 */
int ycp_install(const char *ycp_path);
int ycp_remove(const char *name);              /* 按应用名删 /apps/<name>/ */

int         ycp_count(void);                   /* /apps 下已装应用数 */
const char *ycp_name(int idx);                 /* 第 idx 个应用名 */

/* manifest 迷你 JSON: 提取顶层 "key" 的字符串值(不支持转义;
 * description 内禁用双引号与反斜杠)。找到返回 1。 */
int ycp_json_str(const char *js, const char *key, char *out, int max);

#endif /* YCP_H */
