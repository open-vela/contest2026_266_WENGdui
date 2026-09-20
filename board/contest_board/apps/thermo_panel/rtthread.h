/**
 * @file rtthread.h  (本目录影子头)
 * @brief 把厂家 UI 用到的 RT-Thread 打印 API 映射到 NuttX(C 库)。
 *
 * 【为什么需要】
 *   厂家 app/src/thermo_ui.c 第 19 行 `#include "rtthread.h"`, 并在
 *   show_page / update_* / init 里用 rt_kprintf() 打日志、用 rt_snprintf()
 *   做数字格式化。本工程跑在 NuttX 上, 没有 RT-Thread。
 *
 *   为了【不改厂家 UI 源码一个字】, 这里放一个同名影子头:
 *     #include "rtthread.h" 用引号形式, 先在本文件所在目录查找,
 *     因此只有 apps/thermo_panel 下的源文件会命中它, 不影响 LVGL/NuttX 其它部分。
 *
 *   映射关系 (签名完全一致, 纯改名):
 *     rt_kprintf(fmt, ...)         -> printf(fmt, ...)
 *     rt_snprintf(b, n, fmt, ...)  -> snprintf(b, n, fmt, ...)
 */
#ifndef THERMO_RTTHREAD_SHIM_H
#define THERMO_RTTHREAD_SHIM_H

#include <stdio.h>

#define rt_kprintf   printf
#define rt_snprintf  snprintf

#endif /* THERMO_RTTHREAD_SHIM_H */