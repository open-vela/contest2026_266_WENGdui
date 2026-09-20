/**
 * @file sf32lb52_pinmux.h
 * @brief SiFli SF32LB52X PINMUX 真实寄存器定义
 *
 * 数据来源 (逐条核对自 SDK):
 *   sdk/drivers/cmsis/sf32lb52x/register.h        -> PINMUX1_BASE / HPSYS_CFG_BASE
 *   sdk/drivers/cmsis/sf32lb52x/hpsys_pinmux.h    -> pad 寄存器位域
 *   sdk/drivers/cmsis/sf32lb52x/hpsys_cfg.h       -> USARTx_PINR (引脚选择)
 *   sdk/drivers/cmsis/sf32lb52x/bf0_pin_const.h   -> pad / pin_function 枚举值
 *   sdk/drivers/hal/bf0_hal_pinmux.c              -> HAL_PIN_Set / HAL_PIN_SetUartFunc
 *
 * 关键结论 (52X 的坑: 串口引脚不是"只写 FSEL"即可):
 *   1) 每个 pad 只有一个 4bit 的 FSEL 选择 "功能组", 不是完整的复用编号。
 *      例如 PA18/PA19 的串口功能组都叫 PAxx_I2C_UART, FSEL 值同为 4。
 *   2) 具体把哪一路 USART 接到该 pad, 由 HPSYS_CFG->USARTx_PINR 的
 *      RXD_PIN / TXD_PIN 字段决定 (字段值 = pad - PAD_PA00)。
 *   3) 因此配置 USART1 到 PA18/PA19 必须同时:
 *        - 使能 PINMUX1 时钟 (HPSYS RCC ENR1 bit2)
 *        - USART1_PINR: RXD_PIN=18, TXD_PIN=19
 *        - PAD_PA18 / PAD_PA19: FSEL=4, PE=1, PS=1 (上拉)
 */

#ifndef __ARCH_ARM_SRC_SF32LB52_SF32LB52_PINMUX_H
#define __ARCH_ARM_SRC_SF32LB52_SF32LB52_PINMUX_H

#include <stdint.h>
#include "sf32lb52_memorymap.h"

/* ---- PINMUX 寄存器组 (每 pad 一个 32bit 寄存器) ----
 *
 * 数组下标 = pad 枚举值 - 1 (HAL_PIN_Set: *(pin + subsys_pad_idx - 1))
 */

#define SF32LB52_PINMUX1 ((volatile uint32_t *)SF32LB52_PINMUX1_BASE)
#define SF32LB52_PINMUX2 ((volatile uint32_t *)SF32LB52_PINMUX2_BASE)

/* ---- pad 寄存器位域 (与 hpsys_pinmux.h 完全一致) ---- */

#define PINMUX_FSEL_SHIFT     (0)     /* [3:0]  功能组选择 (共 16 组) */
#define PINMUX_FSEL_MASK      (0xful << PINMUX_FSEL_SHIFT)
#define PINMUX_PE             (1ul << 4)   /* 上下拉使能 */
#define PINMUX_PS             (1ul << 5)   /* 1=上拉 0=下拉 */
#define PINMUX_IE             (1ul << 6)   /* 输入使能 */
#define PINMUX_IS             (1ul << 7)   /* 施密特触发 */
#define PINMUX_SR             (1ul << 8)   /* 压摆率 */
#define PINMUX_DS0            (1ul << 9)
#define PINMUX_DS1            (1ul << 10)
#define PINMUX_POE            (1ul << 11)

/* 与 bf0_hal_pinmux.h 的 PIN_PULLUP / PIN_PULLDOWN / PIN_NOPULL 等价 */
#define PINMUX_NOPULL         (0)
#define PINMUX_PULLUP         (PINMUX_PE | PINMUX_PS)
#define PINMUX_PULLDOWN       (PINMUX_PE)

/* ---- pad 枚举 (bf0_pin_const.h: PIN_PAD_UNDEF_H=0 后顺序递增) ---- */

#define PAD_SA00              (1)
#define PAD_SA12              (13)
#define PAD_PA00              (14)
#define PAD_PA18              (32)
#define PAD_PA19              (33)
#define PAD_PA44              (58)

/* pad 寄存器的字节地址 (供 putreg32/getreg32 使用) */
#define SF32LB52_PAD_ADDR(pad) \
  (SF32LB52_PINMUX1_BASE + (uint32_t)((pad) - 1) * 4)

/* 取 pad 寄存器指针 (供直接解引用使用) */
#define SF32LB52_PAD_REG(pad) \
  ((volatile uint32_t *)SF32LB52_PAD_ADDR(pad))

/* ---- USART1 引脚选择寄存器 (HPSYS_CFG) ----
 *
 * HPSYS_CFG_BASE = 0x5000b000
 * 结构内偏移: USART1_PINR 为第 23 个 32bit (下标 22) -> 0x58
 *   0x00 BMR  0x04 IDR  0x08 SWCR 0x0c SCR  0x10 SYSCR 0x14 RTC_TR
 *   0x18 RTC_DR 0x1c ULPMCR 0x20 DBGR 0x24 MDBGR 0x28 BISTCR 0x2c BISTR
 *   0x30 ROMCR0 0x34 ROMCR1 0x38 ROMCR2 0x3c LPIRQ 0x40 USBCR 0x44 SYS_RSVD
 *   0x48 I2C1_PINR 0x4c I2C2_PINR 0x50 I2C3_PINR 0x54 I2C4_PINR 0x58 USART1_PINR
 */

#define SF32LB52_HPSYS_CFG_USART1_PINR  0x5000b058ul

#define PINR_TXD_PIN_SHIFT    (0)     /* [5:0]  字段值 = pad - PAD_PA00 */
#define PINR_TXD_PIN_MASK     (0x3ful << PINR_TXD_PIN_SHIFT)
#define PINR_RXD_PIN_SHIFT    (8)     /* [13:8] */
#define PINR_RXD_PIN_MASK     (0x3ful << PINR_RXD_PIN_SHIFT)

/* ---- USART1 功能组的 FSEL 值 ----
 *
 * bf0_pin_const.c 中 PA18/PA19 行的第 4 个条目为 PAxx_I2C_UART:
 *   {GPIO_A18, 0, SWDIO, 0, PA18_I2C_UART, ...}
 *   {GPIO_A19, 0, SWCLK, 0, PA19_I2C_UART, ...}
 * 即 FSEL = 4 时, 该 pad 进入 I2C/UART 共用功能组。
 */

#define PINMUX_FSEL_USART1    (4)

/* ---- 本板 USART1 引脚 (与 sdk/customer/boards/sf32lb52-lcd_base 一致) ----
 *
 *   HAL_PIN_Set(PAD_PA18, USART1_RXD, PIN_PULLUP, 1);   // RX
 *   HAL_PIN_Set(PAD_PA19, USART1_TXD, PIN_PULLUP, 1);   // TX
 */

#define SF32LB52_UART1_RX_PAD PAD_PA18
#define SF32LB52_UART1_TX_PAD PAD_PA19

#endif /* __ARCH_ARM_SRC_SF32LB52_SF32LB52_PINMUX_H */