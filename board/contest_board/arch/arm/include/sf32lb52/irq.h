/****************************************************************************
 * arch/arm/include/sf32lb52/irq.h
 *
 * SiFli SF32LB52X (HCPU) 中断号定义
 *
 * 结构参照 arch/arm/include/nrf53/nrf5340_irq_cpuapp.h:
 *   本文件经 configure.sh 链接为 include/arch/chip/irq.h,
 *   由 <arch/irq.h> 间接包含, 并负责提供 NR_IRQS。
 *
 * 编号约定 (与 nrf52/nrf53 一致):
 *   NuttX 的 IRQ 号 == NVIC 中断线号。
 *   中断线 0..15 为 Cortex-M33 内核异常 (由 arm_m 公共层负责);
 *   外设中断从 SF32LB52_IRQ_EXTINT (=16) 开始。
 *
 *   数据来源: sdk/drivers/cmsis/sf32lb52x/register.h (IRQn_Type, HCPU)
 *   该表的 IRQn 从 0 开始 → NuttX 号 = SF32LB52_IRQ_EXTINT + IRQn。
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_SF32LB52_IRQ_H
#define __ARCH_ARM_INCLUDE_SF32LB52_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ---- 中断编号基数 ---- */

#define SF32LB52_IRQ_EXTINT   (16)   /* 内核异常占用的中断线数 */
#define SF32LB52_IRQ_NEXTINT  (99)   /* 外设中断数 (HCPU) */

/* NuttX IRQ 总数: 内核异常 + 外设 */

#define SF32LB52_IRQ_NIRQS    (SF32LB52_IRQ_EXTINT + SF32LB52_IRQ_NEXTINT)
#define NR_IRQS               SF32LB52_IRQ_NIRQS

/* ---- 外设中断 (HCPU) ---- */

#define SF32LB52_IRQ_AON          (SF32LB52_IRQ_EXTINT + 0)
#define SF32LB52_IRQ_BLE_MAC      (SF32LB52_IRQ_EXTINT + 1)
#define SF32LB52_IRQ_DMAC2_CH1    (SF32LB52_IRQ_EXTINT + 2)
#define SF32LB52_IRQ_DMAC2_CH8    (SF32LB52_IRQ_EXTINT + 9)
#define SF32LB52_IRQ_PATCH        (SF32LB52_IRQ_EXTINT + 10)
#define SF32LB52_IRQ_USART4       (SF32LB52_IRQ_EXTINT + 12)
#define SF32LB52_IRQ_USART5       (SF32LB52_IRQ_EXTINT + 13)
#define SF32LB52_IRQ_BTIM3        (SF32LB52_IRQ_EXTINT + 16)
#define SF32LB52_IRQ_BTIM4        (SF32LB52_IRQ_EXTINT + 17)
#define SF32LB52_IRQ_LPTIM3       (SF32LB52_IRQ_EXTINT + 19)
#define SF32LB52_IRQ_GPIO2        (SF32LB52_IRQ_EXTINT + 20)
#define SF32LB52_IRQ_HPSYS0       (SF32LB52_IRQ_EXTINT + 21)
#define SF32LB52_IRQ_HPSYS1       (SF32LB52_IRQ_EXTINT + 22)
#define SF32LB52_IRQ_LPTIM1       (SF32LB52_IRQ_EXTINT + 46)
#define SF32LB52_IRQ_LPTIM2       (SF32LB52_IRQ_EXTINT + 47)
#define SF32LB52_IRQ_PMUC         (SF32LB52_IRQ_EXTINT + 48)
#define SF32LB52_IRQ_RTC          (SF32LB52_IRQ_EXTINT + 49)
#define SF32LB52_IRQ_DMAC1_CH1    (SF32LB52_IRQ_EXTINT + 50)
/* USART1 控制台 RX DMA 用的通道 (DMA1_Channel7)
 * 依据: register.h:132  DMAC1_CH7_IRQn = 56
 *       dma_config.h:274-275 UART1_RX_DMA_IRQ = DMAC1_CH7_IRQn
 * DMA1 通道 1..8 占用中断线 50..57, 故 CH7 = 56。 */
#define SF32LB52_IRQ_DMAC1_CH7    (SF32LB52_IRQ_EXTINT + 56)
#define SF32LB52_IRQ_DMAC1_CH8    (SF32LB52_IRQ_EXTINT + 57)
#define SF32LB52_IRQ_LCPU2HCPU    (SF32LB52_IRQ_EXTINT + 58)
#define SF32LB52_IRQ_USART1       (SF32LB52_IRQ_EXTINT + 59)
#define SF32LB52_IRQ_SPI1         (SF32LB52_IRQ_EXTINT + 60)
#define SF32LB52_IRQ_I2C1         (SF32LB52_IRQ_EXTINT + 61)
#define SF32LB52_IRQ_EPIC         (SF32LB52_IRQ_EXTINT + 62)
#define SF32LB52_IRQ_LCDC1        (SF32LB52_IRQ_EXTINT + 63)
#define SF32LB52_IRQ_I2S1         (SF32LB52_IRQ_EXTINT + 64)
#define SF32LB52_IRQ_GPADC        (SF32LB52_IRQ_EXTINT + 65)
#define SF32LB52_IRQ_EFUSEC       (SF32LB52_IRQ_EXTINT + 66)
#define SF32LB52_IRQ_AES          (SF32LB52_IRQ_EXTINT + 67)
#define SF32LB52_IRQ_PTC1         (SF32LB52_IRQ_EXTINT + 68)
#define SF32LB52_IRQ_TRNG         (SF32LB52_IRQ_EXTINT + 69)
#define SF32LB52_IRQ_GPTIM1       (SF32LB52_IRQ_EXTINT + 70)
#define SF32LB52_IRQ_GPTIM2       (SF32LB52_IRQ_EXTINT + 71)
#define SF32LB52_IRQ_BTIM1        (SF32LB52_IRQ_EXTINT + 72)
#define SF32LB52_IRQ_BTIM2        (SF32LB52_IRQ_EXTINT + 73)
#define SF32LB52_IRQ_USART2       (SF32LB52_IRQ_EXTINT + 74)
#define SF32LB52_IRQ_SPI2         (SF32LB52_IRQ_EXTINT + 75)
#define SF32LB52_IRQ_I2C2         (SF32LB52_IRQ_EXTINT + 76)
#define SF32LB52_IRQ_EXTDMA       (SF32LB52_IRQ_EXTINT + 77)
#define SF32LB52_IRQ_I2C4         (SF32LB52_IRQ_EXTINT + 78)
#define SF32LB52_IRQ_SDMMC1       (SF32LB52_IRQ_EXTINT + 79)
#define SF32LB52_IRQ_PDM1         (SF32LB52_IRQ_EXTINT + 82)
#define SF32LB52_IRQ_GPIO1        (SF32LB52_IRQ_EXTINT + 84)
#define SF32LB52_IRQ_MPI1         (SF32LB52_IRQ_EXTINT + 85)
#define SF32LB52_IRQ_MPI2         (SF32LB52_IRQ_EXTINT + 86)
#define SF32LB52_IRQ_EZIP         (SF32LB52_IRQ_EXTINT + 89)
#define SF32LB52_IRQ_AUDPRC       (SF32LB52_IRQ_EXTINT + 90)
#define SF32LB52_IRQ_TSEN         (SF32LB52_IRQ_EXTINT + 91)
#define SF32LB52_IRQ_USBC         (SF32LB52_IRQ_EXTINT + 92)
#define SF32LB52_IRQ_I2C3         (SF32LB52_IRQ_EXTINT + 93)
#define SF32LB52_IRQ_ATIM1        (SF32LB52_IRQ_EXTINT + 94)
#define SF32LB52_IRQ_USART3       (SF32LB52_IRQ_EXTINT + 95)
#define SF32LB52_IRQ_AUD_HP       (SF32LB52_IRQ_EXTINT + 96)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#endif /* __ARCH_ARM_INCLUDE_SF32LB52_IRQ_H */