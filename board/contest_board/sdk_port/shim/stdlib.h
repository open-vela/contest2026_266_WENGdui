/**
 * @file stdlib.h
 * @brief 给厂商 QSPI/NOR HAL 用的极简 stdlib.h (挡掉 newlib 的那份)
 *
 * 为什么需要 (实测):
 *   厂商 bf0_hal_mpi.c:50 与 bf0_hal_mpi_ex.c:50 都有 #include <stdlib.h>。
 *   本编译环境里 sdk_port/shim/stddef.h 抢在搜索路径最前面, 它按设计
 *   【只给 size_t/ptrdiff_t/NULL/offsetof】, 不含 wchar_t。
 *   于是 newlib 的 stdlib.h 经 sys/reent.h -> sys/_types.h 时炸:
 *       sys/_types.h:167: error: unknown type name 'wint_t'
 *       stdlib.h:111:    error: unknown type name 'wchar_t'
 *   这两个类型只有 newlib 自己的 stddef.h 才带, 而它已被挡掉。
 *
 *   补 newlib 的 stddef.h 不划算 (wchar_t/wint_t/_mbstate_t/struct _reent
 *   会连锁要更多定义)。按本工程既有惯例 (string.h/stddef.h 的同一理由),
 *   直接用一个"抢在最前面"的极简 stdlib.h 挡掉。
 *
 * 厂商这两个文件里没有用到 stdlib 的任何函数 (无 malloc/atoi/abs...),
 * 所以这里只提供最小定义即可。
 *
 * 【重要】与 string.h/stddef.h 同规矩: 绝不 include 任何 NuttX/标准头,
 *   否则 sys/types.h 的 `enum { ERROR = -1 }` 会与厂家 register.h 的
 *   `enum { ERROR = 0 }` 撞车。
 */

#ifndef __SHIM_STDLIB_H
#define __SHIM_STDLIB_H

typedef __SIZE_TYPE__ size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* __SHIM_STDLIB_H */