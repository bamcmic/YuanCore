/* vbe.h - 运行时分辨率切换(QEMU std VGA 的 Bochs dispi 接口)
 * 真机其它显卡多半没有 dispi, set 会"成功"但无效果 -- 实验性选项,
 * desc 里诚实标注。fb.width/height/pitch 同步更新(帧缓冲物理基址不变)。
 */
#ifndef VBE_H
#define VBE_H

/* 0 = 已生效(写入后读回验证通过);非 Bochs/QEMU 环境返回 -1,fb 不变 */
int vbe_set_mode(int w, int h);

/* 启动时争取 1920x1080,逐级回退;真机上 no-op 返回 -1 */
int vbe_try_hires(void);

#endif /* VBE_H */
