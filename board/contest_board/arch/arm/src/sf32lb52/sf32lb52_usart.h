/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_usart.h
 *
 * SF32LB52X USART 寄存器布局与位定义 (HPSYS USART1..3)
 *
 * 依赖 (已在 sf32lb52_memorymap.h 定义):
 *   SF32LB52_USART1_BASE  = 0x50084000
 *
 * 硬件事实 (逐条核对自 SDK):
 *   寄存器布局 = CR1/CR2/CR3/BRR/GTPR/RTOR/RQR/ISR/ICR/RDR/TDR/MISCR
 *                (cmsis/Include/usart.h, 与 STM32 USART 一致)
 *   波特率时钟 = 48MHz 固定 (bf0_hal.h: SystemFixClock)
 *                brr = 48000000 / baud   (bf0_hal_uart.c)
 *
 * 本头文件供 lowputc.c (轮询控制台) 与 serial.c (中断驱动) 共用,
 * 避免两处各自定义寄存器结构而分叉。
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_SF32LB52_SF32LB52_USART_H
#define __ARCH_ARM_SRC_SF32LB52_SF32LB52_USART_H

/****************************************************************************
 * 预处理器定义
 ****************************************************************************/

/* USART 固定时钟 (SystemFixClock) */

#define SF32LB52_SYSTEM_FIX_CLOCK  48000000ul

/* 最小 BRR 值 (低于此需要过采样旁路, 本端口不涉及) */

#define SF32LB52_USART_BRR_MIN     0x10ul

/* CR1 */

#define USART_CR1_UE       (1ul << 0)    /* USART 使能 */
#define USART_CR1_RE       (1ul << 2)    /* 接收使能 */
#define USART_CR1_TE       (1ul << 3)    /* 发送使能 */
#define USART_CR1_RXNEIE   (1ul << 5)    /* 接收非空中断 */
#define USART_CR1_TXEIE    (1ul << 7)    /* 发送空中断 */
#define USART_CR1_OVER8    (1ul << 14)   /* 8 倍过采样 (BRR 过小时启用) */

/* ISR */

#define USART_ISR_PE       (1ul << 0)    /* 校验错误 */
#define USART_ISR_FE       (1ul << 1)    /* 帧错误 */
#define USART_ISR_NE       (1ul << 2)    /* 噪声错误 (SDK: USART_ISR_NF) */
#define USART_ISR_ORE      (1ul << 3)    /* 溢出错误 */
#define USART_ISR_RXNE     (1ul << 5)    /* 接收非空 */
#define USART_ISR_TC       (1ul << 6)    /* 发送完成 */
#define USART_ISR_TXE      (1ul << 7)    /* 发送数据寄存器空 */

/* ICR */

#define USART_ICR_PECF     (1ul << 0)    /* 清校验错误 */
#define USART_ICR_FECF     (1ul << 1)    /* 清帧错误 */
#define USART_ICR_NCF      (1ul << 2)    /* 清噪声错误 (SDK: USART_ICR_NCF) */
#define USART_ICR_ORECF    (1ul << 3)    /* 清溢出错误 */
#define USART_ICR_TCCF     (1ul << 6)    /* 清发送完成 */

/* MISCR */

#define USART_MISCR_SMPLINI_SHIFT   (0)          /* [3:0] 采样起点 */
#define USART_MISCR_SMPLINI_MASK    (0xful << USART_MISCR_SMPLINI_SHIFT)
#define USART_MISCR_SMPLINI_OVS16   (6ul)        /* 16 倍过采样时取 6 */
#define USART_MISCR_SMPLINI_OVS8    (2ul)        /* 8  倍过采样时取 2 */

/****************************************************************************
 * 寄存器结构
 *
 * 偏移与 cmsis/Include/usart.h 中的 USART_TypeDef 对齐。
 * 截取到 MISCR (0x2c) 已足够本端口全部功能。
 ****************************************************************************/

struct sf32lb52_usart_s
{
  volatile uint32_t cr1;    /* 0x00 控制寄存器 1 */
  volatile uint32_t cr2;    /* 0x04 控制寄存器 2 */
  volatile uint32_t cr3;    /* 0x08 控制寄存器 3 */
  volatile uint32_t brr;    /* 0x0c 波特率寄存器 */
  volatile uint32_t gtpr;   /* 0x10 保护时间/预分频 */
  volatile uint32_t rtor;   /* 0x14 接收超时 */
  volatile uint32_t rqr;    /* 0x18 请求寄存器 */
  volatile uint32_t isr;    /* 0x1c 中断与状态 */
  volatile uint32_t icr;    /* 0x20 中断清除 */
  volatile uint32_t rdr;    /* 0x24 接收数据 */
  volatile uint32_t tdr;    /* 0x28 发送数据 */
  volatile uint32_t miscr;  /* 0x2c 杂项控制 */
};

/****************************************************************************
 * 波特率与采样配置 (供 lowputc.c / serial.c 共用)
 *
 * 严格复刻 SDK bf0_hal_uart.c 的时序与参数:
 *   brr = SystemFixClock / baud                       (第 288 行)
 *   brr < 0x10 时: brr = (SystemFixClock << 1) / baud
 *                  置 CR1.OVER8, MISCR.SMPLINI = 2   (8 倍过采样)
 *   否则:           清 CR1.OVER8, MISCR.SMPLINI = 6   (16 倍过采样)
 *
 * 重要: SDK 注释明确要求 "Baudrate need to be set after UART enabled",
 *       因此必须先置 UE, 再写 BRR。
 ****************************************************************************/

static inline void
sf32lb52_usart_setbaud(struct sf32lb52_usart_s *u, uint32_t baud)
{
  uint32_t brr = SF32LB52_SYSTEM_FIX_CLOCK / baud;
  uint32_t cr1;
  uint32_t smplini;

  if (brr < SF32LB52_USART_BRR_MIN)
    {
      brr      = (SF32LB52_SYSTEM_FIX_CLOCK << 1) / baud;
      cr1      = USART_CR1_OVER8;
      smplini  = USART_MISCR_SMPLINI_OVS8;
    }
  else
    {
      cr1      = 0;
      smplini  = USART_MISCR_SMPLINI_OVS16;
    }

  /* 1. 先使能外设 (UE + 收发), 并使 OVER8 与波特率匹配。
   *    用读-改-写, 只动 UE/TE/RE/OVER8 这几位, 其余位保持 ROM 原样 ——
   *    实测证明整寄存器赋值会破坏 ROM 已配好的设置。
   */

  u->cr1 &= ~(USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_OVER8);
  u->cr1 |=  USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | cr1;

  /* 2. 配置采样起点 (仅改 SMPLINI 字段) */

  u->miscr = (u->miscr & ~USART_MISCR_SMPLINI_MASK) | smplini;

  /* 3. 最后写 BRR */

  u->brr = brr;
}

#endif /* __ARCH_ARM_SRC_SF32LB52_SF32LB52_USART_H */