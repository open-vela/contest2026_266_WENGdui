/**
 * @file stdbool.h
 * @brief 给厂商 HAL 用的极简 stdbool.h (挡掉 NuttX 的那份)
 */

#ifndef __SHIM_STDBOOL_H
#define __SHIM_STDBOOL_H

#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#endif

#endif /* __SHIM_STDBOOL_H */