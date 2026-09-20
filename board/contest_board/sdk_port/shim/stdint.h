/**
 * @file stdint.h
 * @brief 给厂商 HAL 用的极简 stdint.h (挡掉 NuttX 的那份)
 *
 * NuttX 的 stdint.h 会定义 _int8_t 等内部类型并依赖它的类型体系; 对
 * 厂商 HAL 这种完全独立的编译单元没必要牵扯进来, 用编译器内建宏最稳。
 */

#ifndef __SHIM_STDINT_H
#define __SHIM_STDINT_H

typedef __INT8_TYPE__       int8_t;
typedef __UINT8_TYPE__      uint8_t;
typedef __INT16_TYPE__      int16_t;
typedef __UINT16_TYPE__     uint16_t;
typedef __INT32_TYPE__      int32_t;
typedef __UINT32_TYPE__     uint32_t;
typedef __INT64_TYPE__      int64_t;
typedef __UINT64_TYPE__     uint64_t;

typedef __INTPTR_TYPE__     intptr_t;
typedef __UINTPTR_TYPE__    uintptr_t;

typedef __INTMAX_TYPE__     intmax_t;
typedef __UINTMAX_TYPE__    uintmax_t;

#define INT8_MAX        __INT8_MAX__
#define UINT8_MAX       __UINT8_MAX__
#define INT16_MAX       __INT16_MAX__
#define UINT16_MAX      __UINT16_MAX__
#define INT32_MAX       __INT32_MAX__
#define UINT32_MAX      __UINT32_MAX__
#define INT64_MAX       __INT64_MAX__
#define UINT64_MAX      __UINT64_MAX__

#define UINT32_C(x)     x##UL
#define UINT16_C(x)     x##U

#endif /* __SHIM_STDINT_H */