/**
 * @file sf32lb52_rcc.h
 * @brief SiFli SF32LB52X HPSYS 时钟控制 (RCC) 真实寄存器定义
 *
 * 数据来源: sdk/drivers/cmsis/sf32lb52x/hpsys_rcc.h
 * 基地址: HPSYS_RCC_BASE = 0x50000000
 *
 * 注意: ENR1 中没有 USART1 使能位 (USART1 为默认调试口),
 *       USART1 复位位在 RSTR1 bit3, 但使能不经 ENR1/ENR2.
 */

#ifndef __ARCH_ARM_SRC_SF32LB52_SF32LB52_RCC_H
#define __ARCH_ARM_SRC_SF32LB52_SF32LB52_RCC_H

#include <stdint.h>
#include "sf32lb52_memorymap.h"

/* ---- 寄存器结构 (偏移顺序严格取自 hpsys_rcc.h) ---- */

struct sf32lb52_hpsys_rcc_s
{
  volatile uint32_t rstr1;    /* 0x00 */
  volatile uint32_t rstr2;    /* 0x04 */
  volatile uint32_t enr1;     /* 0x08 */
  volatile uint32_t enr2;     /* 0x0c */
  volatile uint32_t esr1;     /* 0x10 */
  volatile uint32_t esr2;     /* 0x14 */
  volatile uint32_t ecr1;     /* 0x18 */
  volatile uint32_t ecr2;     /* 0x1c */
  volatile uint32_t csr;      /* 0x20 */
  volatile uint32_t cfgr;     /* 0x24 */
  volatile uint32_t usbcr;    /* 0x28 */
  volatile uint32_t dll1cr;   /* 0x2c */
  volatile uint32_t dll2cr;   /* 0x30 */
  volatile uint32_t hrccal1;  /* 0x34 */
  volatile uint32_t hrccal2;  /* 0x38 */
  volatile uint32_t dbgclkr;  /* 0x3c */
  volatile uint32_t dbgr;     /* 0x40 */
  volatile uint32_t dwcfgr;   /* 0x44 */
  volatile uint32_t reserved[13];
  volatile uint32_t testr;
};

#define SF32LB52_HPSYS_RCC \
  ((struct sf32lb52_hpsys_rcc_s *)SF32LB52_HPSYS_RCC_BASE)

/* ---- ENR1 使能位 ---- */

#define RCC_ENR1_PINMUX1      (1ul << 2)
#define RCC_ENR1_LCDC1        (1ul << 7)
#define RCC_ENR1_I2S1         (1ul << 8)
#define RCC_ENR1_SYSCFG1      (1ul << 10)
#define RCC_ENR1_GPTIM1       (1ul << 15)
#define RCC_ENR1_GPTIM2       (1ul << 16)
#define RCC_ENR1_BTIM1        (1ul << 17)
#define RCC_ENR1_BTIM2        (1ul << 18)
#define RCC_ENR1_SPI1         (1ul << 20)
#define RCC_ENR1_SPI2         (1ul << 21)
#define RCC_ENR1_EXTDMA       (1ul << 22)
#define RCC_ENR1_PDM1         (1ul << 25)
#define RCC_ENR1_I2C1         (1ul << 27)
#define RCC_ENR1_I2C2         (1ul << 28)
#define RCC_ENR1_PTC1         (1ul << 31)

/* ---- ENR2 使能位 ---- */

#define RCC_ENR2_GPIO1        (1ul << 0)
#define RCC_ENR2_MPI1         (1ul << 1)
#define RCC_ENR2_MPI2         (1ul << 2)
#define RCC_ENR2_SDMMC1       (1ul << 4)
#define RCC_ENR2_I2C3         (1ul << 8)
#define RCC_ENR2_ATIM1        (1ul << 9)
#define RCC_ENR2_USART3       (1ul << 12)
#define RCC_ENR2_GPADC        (1ul << 22)
#define RCC_ENR2_TSEN         (1ul << 23)
#define RCC_ENR2_I2C4         (1ul << 25)

/* ---- RSTR1 复位位 (USART1 仅在此处出现) ---- */

#define RCC_RSTR1_USART1      (1ul << 3)
#define RCC_RSTR1_USART2      (1ul << 4)
#define RCC_RSTR1_LCDC1       (1ul << 7)

#define RCC_RSTR2_GPIO1       (1ul << 0)
#define RCC_RSTR2_MPI2        (1ul << 2)
#define RCC_RSTR2_USART3      (1ul << 12)
#define RCC_RSTR2_GPADC       (1ul << 22)

/* ---- LPSYS RCC (GPIO2 归属) ---- */

struct sf32lb52_lpsys_rcc_s
{
  volatile uint32_t rstr1;
  volatile uint32_t rstr2;
  volatile uint32_t enr1;
  volatile uint32_t enr2;
  volatile uint32_t esr1;
  volatile uint32_t esr2;
  volatile uint32_t ecr1;
  volatile uint32_t ecr2;
  volatile uint32_t csr;
  volatile uint32_t cfgr;
};

#define SF32LB52_LPSYS_RCC \
  ((struct sf32lb52_lpsys_rcc_s *)SF32LB52_LPSYS_RCC_BASE)

#endif /* __ARCH_ARM_SRC_SF32LB52_SF32LB52_RCC_H */