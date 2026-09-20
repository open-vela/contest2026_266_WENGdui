/**
 * @file string.h
 * @brief 给厂商 LCDC HAL 用的极简 string.h (挡掉 NuttX 的那份)
 *
 * 为什么需要:
 *   厂商 bf0_hal_lcdc.c 里有 #include <string.h>。在 NuttX 下这会拉进
 *   nuttx/include/string.h -> stddef.h -> sys/types.h, 而 sys/types.h
 *   定义了 `enum { ERROR = -1 }`; 厂商 register.h 里也有 `enum { ERROR = 0 }`
 *   ⇒ 枚举重定义, 编译失败。
 *
 *   本文件抢在搜索路径最前面, 只声明厂商代码需要的几个函数。
 *
 * 【重要】这里绝对不要 include 任何 NuttX 或标准头文件:
 *   - <stddef.h> 会拉进 sys/types.h, 冲突就回来了 (踩过一次)
 *   size_t 直接用编译器内建宏 __SIZE_TYPE__, 零依赖。
 */

#ifndef __SHIM_STRING_H
#define __SHIM_STRING_H

typedef __SIZE_TYPE__ size_t;

void  *memcpy(void *dest, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char  *strcpy(char *dest, const char *src);

#endif /* __SHIM_STRING_H */