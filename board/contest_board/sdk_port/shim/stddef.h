/**
 * @file stddef.h
 * @brief 给厂商 HAL 用的极简 stddef.h (挡掉 NuttX 的那份)
 *
 * 厂商 bf0_hal_def.h:66 会 #include <stddef.h>, 在 NuttX 下会经
 * stddef.h -> sys/types.h 引入 `enum { ERROR = -1 }`, 与厂商 register.h
 * 的 `enum { ERROR = 0 }` 冲突。这里用编译器内建宏提供最小定义, 零依赖。
 */

#ifndef __SHIM_STDDEF_H
#define __SHIM_STDDEF_H

typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* __SHIM_STDDEF_H */