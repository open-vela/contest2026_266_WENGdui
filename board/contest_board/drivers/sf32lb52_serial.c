/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_serial.c
 *
 * NuttX 串口驱动 (USART1 控制台)
 *
 * 硬件事实 (逐条核对自 SDK, 非推测):
 *   USART1_BASE   = 0x50084000            (sf32lb52x/register.h)
 *   USART1_IRQ    = 59                    (sf32lb52x/register.h IRQn_Type)
 *   寄存器布局     = CR1/CR2/CR3/BRR/GTPR/RTOR/RQR/ISR/ICR/RDR/TDR/MISCR
 *                   (cmsis/Include/usart.h, 与 STM32 一致)
 *   波特率时钟     = 48MHz 固定 (bf0_hal.h: SystemFixClock)
 *                   brr = 48000000 / baud  (bf0_hal_uart.c)
 *   引脚           = PA18=RX / PA19=TX, 由 lowputc.c 的 pinmux 初始化完成
 *
 * NuttX 侧约定 (依据 openvela nrf53 端口):
 *   入口函数 arm_serialinit(); 通过 uart_register() 注册 /dev/console。
 *
 * 待 M1 编译核对:
 *   uart_ops_s 的字段名在较新 NuttX 中已将 rxready 更名为 rxavailable,
 *   本文件按较新命名书写, 若 openvela 版本不同需按
 *   nuttx/include/nuttx/serial/serial.h 实际定义校正。
 ****************************************************************************/

#include <nuttx/config.h>
#include <syslog.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>
#include <nuttx/kthread.h>
#include <unistd.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "sf32lb52_memorymap.h"
#include "sf32lb52_irq.h"
#include "sf32lb52_usart.h"





#ifdef CONFIG_ARCH_LOWPUTC
void sf32lb52_lowputc_initialize(uint32_t baud);
#endif

/* 控制台设备节点路径 */
#define SF32LB52_CONSOLE_DEV  "/dev/console"

/* 前向声明: 中断处理函数 (定义见文件末尾) */
static int sf32lb52_interrupt(int irq, void *context, void *arg);

/* 寄存器结构 (sf32lb52_usart_s) 与 CR1/ISR/ICR 位定义
 * 统一来自 sf32lb52_usart.h, 与 lowputc.c 共用, 避免定义分叉。 */

/****************************************************************************
 * 私有数据结构
 ****************************************************************************/

/****************************************************************************
 * 驱动内 RX 通路: 厂家方案 —— DMA 循环搬运 + UART IDLE 中断
 *
 * 【硬件事实, 逐条核对自 SDK 原文】
 *   该 USART 没有硬件 RX FIFO(bf0_hal_uart.h 的 USART_CR1_FIFOEN 只在
 *   _SIFLI_DOXYGEN_ 下存在, USART_TypeDef 里也没有 FCR), RDR 只有 1 字节。
 *   厂家因此在 1Mbps 下用 DMA 把 RDR 直接搬进内存环形缓冲, CPU 不参与
 *   逐字节搬运, 所以再快也不会因 RDR 被覆盖而丢字符。
 *
 *   DMAC1 基址          = 0x50081000
 *                         (sf32lb52_memorymap.h:94 / register.h:414)
 *   通道寄存器布局       = 每通道 5 个字 CCR/CNDTR/CPAR/CM0AR/CBSR
 *                         (cmsis/sf32lb52x/dmac.h DMAC_TypeDef)
 *   USART1 RX 通道       = DMA1_Channel7
 *                         (customer/boards/include/config/sf32lb52x/
 *                          dma_config.h:274 UART1_RX_DMA_INSTANCE)
 *   USART1 RX 请求号     = DMA_REQUEST_5 (dma_config.h:273)
 *   USART1 RX DMA 中断   = DMAC1_CH7_IRQn = 56 (dma_config.h:275,
 *                         register.h:132) -> NuttX IRQ = 16 + 56 = 72
 *   请求号落点           = CSELR2.C7S = 位[21:16]
 *                         (bf0_hal_dma.c:130-137: channel7 -> index2 ->
 *                          写 CSELR2, 字段偏移 2 * 8 = 16)
 *   搬运模式             = DMA_CIRCULAR (bf0_hal_uart.c:1381)
 *   方向/宽度            = 外设->内存, 逐字节, 外设地址固定/MINC 自增
 *                         (bf0_hal_uart.c:1370-1407 HAL_UART_DmaTransmit;
 *                          bf0_hal_dma.c:104-115 CCR 组装)
 *   优先级               = DMA_PRIORITY_MEDIUM (bf0_hal_uart.c:1382)
 *   IDLE 中断配合        = drv_usart.c:355-361 开 UART_IT_IDLE;
 *                         drv_usart.c:403-418 中断里清 IDLE 后
 *                         用 bufsz - CNDTR 算出"已搬了多少", 把新增段
 *                         交给上层
 *   UART 侧位            = CR1.IDLEIE(位4)/ISR.IDLE(位4)/ICR.IDLECF(位4)
 *                         (cmsis/Include/usart.h:84 / 327 / 398)
 *                         CR3.DMAR(位6)  (cmsis/Include/usart.h:222)
 *
 * 【为什么不直接调厂家的 HAL_DMA_Init/Start_IT】
 *   bf0_hal_dma.c 在 HCPU 上编译时 DMA_SUPPORT_DYN_CHANNEL_ALLOC 是打开的
 *   (bf0_hal_dma.h:58-64), 会把 DMA_AllocChannel/FreeChannel 与 DMAC 各通道
 *   IRQ 处理器一并拖入, 还要 HAL_ASSERT / HAL_NVIC_* / HAL_DisableInterrupt /
 *   mpu_dcache_clean / HAL_GetTick 一串外部符号; 厂家 HAL_UART_* 更是要连
 *   bf0_hal_uart.c 一起编。而厂家真正做的事就是下面这十几个寄存器写 —
 *   按原文寄存器级复刻: 零新增依赖、零弱桩, 也就不必动
 *   build_vendor_lcd_lib.sh / 板级 src/Makefile。
 *
 * 【数据流】DMA 把字节循环写进 g_rx_dma_buffer(256B);
 *   中断里用 CNDTR 算出"已搬字节数" -> 提交为 g_rx_dma_head;
 *   receive() 只从该环形缓冲按 g_rx_dma_tail 取数, 绝不再读 RDR
 *   (读 RDR 会与 DMA 抢字节)。
 ****************************************************************************/

/* --- DMAC1 寄存器 (sf32lb52x/dmac.h 原文布局) --- */

#define SF32LB52_DMAC1_BASE_Z  0x50081000ul
#define SF32LB52_DMA_CH        6u    /* 0 基: 对应通道 7 */
#define SF32LB52_DMA_REQ_UART1_RX  5u

struct sf32lb52_dmach_s
{
  volatile uint32_t ccr;    /* 0x00 */
  volatile uint32_t cndtr;  /* 0x04 */
  volatile uint32_t cpar;   /* 0x08 */
  volatile uint32_t cm0ar;  /* 0x0c */
  volatile uint32_t cbsr;   /* 0x10 */
};

struct sf32lb52_dmac_s
{
  volatile uint32_t isr;    /* 0x00 */
  volatile uint32_t ifcr;   /* 0x04 */
  struct sf32lb52_dmach_s ch[8];  /* 0x08..0xa4 */
  volatile uint32_t cselr1; /* 0xa8 */
  volatile uint32_t cselr2; /* 0xac */
  volatile uint32_t dbgsel; /* 0xb0 */
};

#define DMAC_ISR_GIF(n)   (1ul << (4u * (n) + 0u))
#define DMAC_ISR_TCIF(n)  (1ul << (4u * (n) + 1u))
#define DMAC_ISR_HTIF(n)  (1ul << (4u * (n) + 2u))
#define DMAC_ISR_TEIF(n)  (1ul << (4u * (n) + 3u))

#define DMAC_CCR_EN          (1ul << 0)
#define DMAC_CCR_TCIE        (1ul << 1)
#define DMAC_CCR_HTIE        (1ul << 2)
#define DMAC_CCR_TEIE        (1ul << 3)
#define DMAC_CCR_DIR         (1ul << 4)   /* 1 = MEM->PERIPH, 0 = PERIPH->MEM */
#define DMAC_CCR_CIRC        (1ul << 5)
#define DMAC_CCR_PINC        (1ul << 6)
#define DMAC_CCR_MINC        (1ul << 7)
#define DMAC_CCR_PSIZE_MSK   (0x3ul << 8) /* 字段掩码; byte(00b) 时不得 OR 进 CCR */
#define DMAC_CCR_MSIZE_MSK   (0x3ul << 10)/* 字段掩码; byte(00b) 时不得 OR 进 CCR */
#define DMAC_CCR_PL_MEDIUM   (0x1ul << 12)
#define DMAC_CCR_MEM2MEM     (1ul << 14)

/* --- USART 侧 IDLE / DMA 位 (cmsis/Include/usart.h 原文) --- */

#define USART_CR1_IDLEIE   (1ul << 4)
#define USART_CR3_DMAR     (1ul << 6)
#define USART_ISR_IDLE     (1ul << 4)
#define USART_ICR_IDLECF   (1ul << 4)

/* --- DMA 环形缓冲 --- */

#define SF32LB52_RX_DMA_SIZE   256
#define SF32LB52_RX_DMA_MASK   (SF32LB52_RX_DMA_SIZE - 1)

/* receive() 兜底自旋上限: 正常一趟 uart_recvchars() 就排空,
 * 只有框架的 recv 环形缓冲已满时才会多跑几趟(此时再跑也不会变好),
 * 因此上限取 4 —— 既兜住"框架提前收手", 又绝不在中断里长自旋。 */

#define SF32LB52_RX_REPUMP_MAX  4

static uint8_t  g_rx_dma_buffer[SF32LB52_RX_DMA_SIZE];
static volatile uint32_t g_rx_dma_head;    /* 累计"已提交"字节数 (自由运行计数) */
static volatile uint32_t g_rx_dma_tail;    /* 累计"已交给框架"字节数 (自由运行计数) */
static volatile uint32_t g_rx_dma_last;    /* 厂家 last_index: 上次提交的 DMA 累计位置 */
static volatile uint32_t g_rx_dma_inited;
static struct sf32lb52_dmac_s * const g_dmac =
  (struct sf32lb52_dmac_s *)SF32LB52_DMAC1_BASE_Z;

struct sf32lb52_uart_s
{
  struct sf32lb52_usart_s *usart;
  uint32_t baud;
  uint32_t irq;
  uint8_t  irq_attached;
};

/* sf32lb52_send() 写 TDR 前等 TXE 的空转上限。
 *
 * 【为什么必须很小】send() 跑在 uart_xmitchars() 的 PRIMASK=1 临界区内
 * (serial_io.c: uart_spinlock(dev,true) -> rspin_lock_irqsave)。这里每多
 * 空转一轮, RXNE 就晚一轮被响应。HCPU 240MHz 下按每轮约 10 个周期估算,
 * 256 轮 ≈ 2.5k 周期 ≈ 10us 量级, 是"能被接受的最坏窗口";
 * 正常情况 TXE 已置位, 循环 0 次就退出。
 *
 * 【绝不要改成等 TC】TC 是"整字符移完", 115200 下 87us —— 那正是旧实现
 * 把临界区撑到 87us、导致 RX 丢字节的原因(厂家 sifli_putc 就是等 TC)。 */

#define SF32LB52_SEND_TXE_MAX   256ul

/****************************************************************************
 * RX 诊断计数 (只统计, 不参与收发判决)
 *
 * 目的: 定位 115200 下 help(4 字符) 只收到 "he" 的丢字节位置, 以及改造
 *   RX DMA 之后对比"改造前后"的表现。全部为单调递增的 volatile 计数,
 *   只在中断上下文自增。
 *
 * 【为什么放在文件级而不是 priv 结构体里】为了把它们暴露给外部
 *   (NSH 命令 rx / rxd -> sf32lb52_rxdiag_dump()), 统一提升为文件级
 *   static volatile, 驱动内部与 dump 函数读同一份数据。
 *
 * 【RX 改成 DMA 之后各计数的含义(名字保持原样, 便于与改造前逐项对比)】
 *   g_rx_isr_count       —— 进入中断次数(USART1 与 DMAC1_CH7 两条线合计)
 *   g_rx_raw_read        —— DMA 已搬进 g_rx_dma_buffer 的字节数(CNDTR 增量)
 *   g_rx_staged_drop     —— 环形缓冲疑似被 DMA 套圈而丢掉的字节数
 *   g_rx_to_framework    —— 经 receive() 交给 NuttX 框架的字节数
 *   g_rx_ore_count       —— 观测到 ORE(接收溢出) 的次数
 *   g_rx_framing_err_count —— 观测到 FE(帧错误) 的次数
 *   g_rx_stage_repump    —— 单次中断里"多跑一趟 uart_recvchars 才排空"的次数
 ****************************************************************************/

static volatile uint32_t g_rx_isr_count;         /* 进入中断次数 */
static volatile uint32_t g_rx_raw_read;          /* DMA 搬入环形缓冲的字节数 */
static volatile uint32_t g_rx_staged_drop;       /* 环形缓冲被套圈而丢掉的字节数 */
static volatile uint32_t g_rx_to_framework;      /* 经 receive() 交给框架的字节数 */
static volatile uint32_t g_rx_ore_count;         /* 观测到 ORE(接收溢出) 的次数 */
static volatile uint32_t g_rx_framing_err_count; /* 观测到 FE(帧错误) 的次数 */
static volatile uint32_t g_rx_stage_repump;      /* 单次中断内"多跑一趟才排空"的次数 */

static struct sf32lb52_uart_s g_usart1_priv =
{
  .usart        = (struct sf32lb52_usart_s *)SF32LB52_USART1_BASE,
  .baud         = BOARD_CONSOLE_BAUD,
  .irq          = SF32LB52_IRQ_USART1,
  .irq_attached = 0,
};

/****************************************************************************
 * uart_ops_s 实现
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_rxdma_start
 *
 * Description:
 *   按厂家原文把 USART1 RX 接到 DMA1_Channel7 上: 循环模式, 逐字节,
 *   外设->内存, 并在 USART 侧打开 IDLE 中断与 CR3.DMAR。
 *   幂等: 重复调用只做一次。
 ****************************************************************************/

static int sf32lb52_rxdma_start(struct sf32lb52_uart_s *priv)
{
  struct sf32lb52_usart_s *u = priv->usart;
  struct sf32lb52_dmach_s *ch = &g_dmac->ch[SF32LB52_DMA_CH];

  if (g_rx_dma_inited)
    {
      return OK;
    }

  /* 1. 通道 7 的请求源选 USART1_RX: CSELR2.C7S = 位[21:16] (原文见文件头注释) */

  g_dmac->cselr2 &= ~(0x3ful << 16);
  g_dmac->cselr2 |=  ((uint32_t)SF32LB52_DMA_REQ_UART1_RX << 16);

  /* 2. 配置前必须 EN=0 —— 原文 HAL_DMA_DeInit 就是先写 CCR=0,
   *    且 CNDTR/CPAR/CM0AR 只在通道关闭时才可写。 */

  ch->ccr   = 0;

  /* 3. 源 = USART1 的 RDR(外设地址固定), 目的 = 环形缓冲, 长度 = 整缓冲。
   *    缓冲区大小约定与厂家一致: DMA 搬运"整个 bufsz",
   *    已搬字节数由 CPU 用 CNDTR 反算 (drv_usart.c:408)。 */

  ch->cpar  = (uint32_t)&u->rdr;
  ch->cm0ar = (uint32_t)g_rx_dma_buffer;
  ch->cndtr = SF32LB52_RX_DMA_SIZE;
  ch->cbsr  = 0;

  /* 4. 清本通道全部中断标志 (GIF/TCIF/HTIF/TEIF) */

  g_dmac->ifcr = DMAC_ISR_GIF(SF32LB52_DMA_CH)  |
                 DMAC_ISR_TCIF(SF32LB52_DMA_CH) |
                 DMAC_ISR_HTIF(SF32LB52_DMA_CH) |
                 DMAC_ISR_TEIF(SF32LB52_DMA_CH);

  /* 5. CCR: 循环 + MINC + 逐字节(PSIZE/MSIZE=00b) + 中优先级 + HT/TC/TE 中断。
   *    DIR=0(PERIPH->MEM), PINC=0(外设地址固定), MEM2MEM=0, 全部按原文取值。
   *
   * 【宽度必须保持 00b(byte), 绝不能 OR 进 PSIZE/MSIZE 的"字段掩码"】
   *   厂家是"先清字段再 OR 对齐值"(bf0_hal_dma.c:103-112), 而
   *     DMA_PDATAALIGN_BYTE = 0x0 (bf0_hal_dma.h:464)
   *     DMA_MDATAALIGN_BYTE = 0x0 (bf0_hal_dma.h:474)
   *   即 byte = 00b。若把 DMAC_CCR_PSIZE_MSK(0x300)/MSIZE_MSK(0xC00)
   *   OR 进来, 就把两个字段写成了 11b(32 位/保留): DMA 每次搬一个字,
   *   一个字符会写 4 个内存字节, 于是 CNDTR 反算出来的"已搬字节数"与
   *   缓冲区真实内容错位 —— 正是"框架只拿到最后那一个 \r"的根因。
   *
   * 【中断】与厂家 HAL_DMA_Start_IT 在 RX 上的 TC|HT|TE 一致
   *   (bf0_hal_dma.c:899-908): 半满、完成(整圈)、错误都要能触发提交,
   *   不能只留 HT。 */

  ch->ccr = DMAC_CCR_CIRC | DMAC_CCR_MINC |
            DMAC_CCR_HTIE | DMAC_CCR_TCIE | DMAC_CCR_TEIE |
            DMAC_CCR_PL_MEDIUM;              /* PSIZE=MSIZE=00b (byte) */
  ch->ccr |= DMAC_CCR_EN;

  /* 6. USART 侧: 关 RXNEIE(DMA 负责取 RDR, 再开逐字节中断只会打断 DMA),
   *    开 IDLE 中断, 开 CR3.DMAR。顺序与厂家一致: 先起 DMA, 再开 DMAR。 */

  u->icr = USART_ICR_IDLECF;
  u->cr1 &= ~USART_CR1_RXNEIE;
  u->cr1 |=  USART_CR1_IDLEIE;
  u->cr3 |=  USART_CR3_DMAR;

  g_rx_dma_head  = 0;
  g_rx_dma_tail  = 0;
  g_rx_dma_last  = 0;
  g_rx_dma_inited = 1;

  return OK;
}

static int sf32lb52_setup(struct uart_dev_s *dev)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;
  struct sf32lb52_usart_s *u = priv->usart;



  /* 关收发, 配置格式 8-N-1 */

  /* M1: 不做整寄存器清零 —— 会推翻 ROM/bootloader 配好的
   * 8-N-1 / 1Mbps 帧格式, 实测导致 E 之后输出乱码。 */

  /* 清除遗留错误标志 */

  u->icr = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_TCCF;

  /* 波特率 + 采样配置 (含 "先 UE 后 BRR" 时序, 见 sf32lb52_usart.h) */

  /* 只置 TE/RE/UE, 其余位(帧格式/波特率)保持 ROM 原样。
   * 厂家 HAL 全程只用 MODIFY_REG, 从不清无关位。 */
  u->cr1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);

  /* RX 通路改成 DMA 循环搬运 + IDLE 中断 (厂家方案, 见文件头注释)。
   * 放在最后: UE/TE/RE 先就位, DMA 再接管 RDR 取数。 */

  return sf32lb52_rxdma_start(priv);
}

static void sf32lb52_shutdown(struct uart_dev_s *dev)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;

  /* 【M1 真凶: 绝不能把 CR1 整个清零】
   *
   * 实测证据 (probe v2):
   *   [P1-lateinit] open(/dev/console)=3   <-- 输出就停在这之后的 close(fd)
   * 因为 close() 会走到本函数, 而 `priv->usart->cr1 = 0` 把 UART 直接关死,
   * 之后再也不会有任何输出。
   *
   * NSH 启动时必然要 open 控制台(并可能关闭/重开), 一旦走到本函数控制台
   * 就死了 —— 现象正是"board bringup 之后彻底安静, 没有 NSH 横幅、没有
   * nsh> 提示符"。
   *
   * 控制台必须始终保持可用, 因此这里不做任何寄存器操作:
   * ROM/bootloader 配好的帧格式与波特率原样保留, UE/TE/RE 也不动。
   */

  (void)priv;
}

static int sf32lb52_attach(struct uart_dev_s *dev)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;
  int ret;

  /* 【RX DMA 之后要接两条中断线】
   *   priv->irq                 = USART1 (IDLE / TXE / 错误标志)
   *   SF32LB52_IRQ_DMAC1_CH7    = 通道半满/完成 (厂家用 HT+TC 回调,
   *                               见 drv_usart.c:472-502)
   * 两条线共用同一个处理函数: 里面读的是同一个 CNDTR, 谁先到都一样。 */

  if (!priv->irq_attached)
    {
      ret = irq_attach(priv->irq, sf32lb52_interrupt, dev);
      if (ret < 0)
        {
          return ret;
        }

      ret = irq_attach(SF32LB52_IRQ_DMAC1_CH7, sf32lb52_interrupt, dev);
      if (ret < 0)
        {
          irq_detach(priv->irq);
          return ret;
        }

      priv->irq_attached = 1;
    }

  up_enable_irq(priv->irq);
  up_enable_irq(SF32LB52_IRQ_DMAC1_CH7);
  return OK;
}

static void sf32lb52_detach(struct uart_dev_s *dev)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;

  up_disable_irq(priv->irq);
  up_disable_irq(SF32LB52_IRQ_DMAC1_CH7);

  if (priv->irq_attached)
    {
      irq_detach(priv->irq);
      irq_detach(SF32LB52_IRQ_DMAC1_CH7);
      priv->irq_attached = 0;
    }
}

/* 注意: 本源码树中 uart_ops_s.ioctl 的原型是
 *   int (*ioctl)(FAR struct file *filep, int cmd, unsigned long arg)
 * 第一个参数是 struct file *, 不是 struct uart_dev_s *,
 * 否则报 incompatible pointer type。
 */

static int sf32lb52_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static int sf32lb52_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;
  struct sf32lb52_usart_s *u = priv->usart;
  uint32_t ch;

  /* 【取数位置】只从 DMA 环形缓冲取 —— 就是厂家"CPU 用 CNDTR 反算已搬
   *   字节数, 再把新增段交给上层"(drv_usart.c:403-418)的同一套约定。
   *
   * 【绝不再读 RDR】RDR 已由 DMA 独占取数; CPU 再读会把 DMA 尚未搬走的
   *   那一个字节抢走, 反而制造丢字节。因此这里删掉了旧的
   *   "暂存区空则读 RDR" 分支。
   *
   * 【关键】ORE/FE/PE/NE 是粘性标志位, 只有写 ICR 才会清! 这里依旧清掉
   *   标志并把 status 归零, 让字符正常入库; 错误只做统计, 不让上层
   *   据此丢弃字符 (M1 已验证的行为, 保持不变)。 */

  if (g_rx_dma_head != g_rx_dma_tail)
    {
      /* tail 是自由运行计数, 落到环形缓冲时才取模 —— 取数跨过缓冲区末尾
       * (回绕)时无需特判, 逐字节取数天然把"回绕增量"分成两段搬走,
       * 与厂家"提交增量 + 环形读指针"的语义一致。 */

      ch = g_rx_dma_buffer[g_rx_dma_tail & SF32LB52_RX_DMA_MASK];
      g_rx_dma_tail++;
    }
  else
    {
      /* 理论上到不了: 本函数只被 uart_recvchars() 在 rxavailable()==true
       * 时调用。兜底返回 0 而不是去读 RDR。 */

      ch = 0;
    }

  /* 诊断: 本函数是 uart_recvchars() 取数的唯一出口, 每返回一个字节
   * 就记一次 —— "交给框架的字节数"。 */

  g_rx_to_framework++;

  u->icr = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NCF;

  *status = 0;
  return (int)(ch & 0xff);
}


static bool sf32lb52_rxavailable(struct uart_dev_s *dev)
{
  /* DMA 环形缓冲里还有"已搬入但未被框架取走"的字节就算可读。
   * uart_recvchars() 会一直循环到取空为止。 */

  (void)dev;
  return g_rx_dma_head != g_rx_dma_tail;
}

static void sf32lb52_rxint(struct uart_dev_s *dev, bool enable)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;

  if (enable)
    {
      /* RX 走 DMA 之后, 接收中断的主角是 IDLE 中断(CR1.IDLEIE);
       * RXNEIE 必须保持关闭 —— DMA 负责取 RDR, 再开逐字节中断只会
       * 把 CPU 拉回"逐字节搬运"的老路。 */

      priv->usart->cr1 &= ~USART_CR1_RXNEIE;
      priv->usart->cr1 |=  USART_CR1_IDLEIE;
    }
  else
    {
      /* 【故意不关】接收中断。
       *
       * NuttX 框架在"环形缓冲空、需要等待数据"时会调用 uart_disablerxint(dev)
       * (它就是 rxint(dev,false) 的宏)。若这里真的把 IDLEIE 关掉, 那么从"关"
       * 到下次 read() 阻塞重新打开的这段时间里到来的数据虽然会被 DMA 搬进
       * 内存, 却不会有人来取; 控制台场景让接收中断常开最稳。
       * RXNEIE 本来就没开, 这里也不动它 (M1 约定: 关接收中断不得把 RX 打死)。
       *
       * 【注意】绝不要在这个函数里做串口打印! 打印一行要几毫秒, 正好卡在
       * 接收路径中间, 会把收到的字符全冲掉(踩过的坑)。
       */

      (void)priv;
    }
}




static bool sf32lb52_txready(struct uart_dev_s *dev)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;

  return (priv->usart->isr & USART_ISR_TXE) != 0;
}

static void sf32lb52_txint(struct uart_dev_s *dev, bool enable)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;

  if (enable)
    {
      priv->usart->cr1 |= USART_CR1_TXEIE;
    }
  else
    {
      priv->usart->cr1 &= ~USART_CR1_TXEIE;
    }
}

/* 【M4 修复: 写 TDR 前必须等 TXE, 但只做有界短等】
 *
 * 上一版为压缩 PRIMASK 临界区, 把本函数改成"只写一次 TDR、什么都不等",
 * 结果连发(回显/换行展开)时第 2 个字节直接覆盖尚未被移位寄存器取走的
 * 第 1 个字节 —— 现象正是 nsh> 下发 help(4 字符), 回显只剩 "he"。
 *
 * 【厂家两种实现的对比】(源码/xiaozhi-sf32-1.4.0/sdk)
 *
 *   a) drv_usart.c:289-309  sifli_putc()
 *        UART_INSTANCE_CLEAR_FUNCTION(&(uart->handle), UART_FLAG_TC);
 *        __HAL_UART_PUTC(&uart->handle, c);          // 宏 = Instance->TDR = c
 *        while (__HAL_UART_GET_FLAG(&(uart->handle), UART_FLAG_TC) == RESET);
 *      它等的是 【TC】(整字符移完)。而且在 rt_hw_interrupt_disable() 里等,
 *      115200 下最坏就是一个字符时间 87us —— 旧实现"等 TC"的出处就是这里。
 *
 *   b) bf0_hal_uart.c:982-1043  HAL_UART_Transmit()
 *        while (TxXferCount > 0) {
 *            UART_WaitOnFlagUntilTimeout(huart, UART_FLAG_TXE, RESET, ...); // 1009
 *            huart->Instance->TDR = (*pData++ & 0xFF);                      // 1021
 *        }
 *        UART_WaitOnFlagUntilTimeout(huart, UART_FLAG_TC, RESET, ...);      // 1026
 *      它写 TDR 前等的是 【TXE】(TDR 空, 只需移位寄存器取走上一字节, 几个
 *      周期即可), 只在整段搬运结束时才等一次 TC。这正是本函数要照抄的做法:
 *      逐字节路径只等 TXE, 等 TC 的事交给框架/中断。
 *
 * 【为什么现在可以安全地只等 TXE】
 *   TXE 只表示"TDR 已被移位寄存器取走", 通常进来时已经是 1, 循环一次就退出;
 *   最坏也只是等一个字符时间。所以用 SF32LB52_SEND_TXE_MAX 封顶, 越界即放弃,
 *   绝不把关中断窗口拖长 —— 绝不等待 TC(87us), 更不做任何打印。
 *
 * 【换行】'\n' -> '\r\n' 依旧交给框架的 ONLCR (见 g_usart1_dev.tc_oflag),
 * 本函数不补 '\r', 否则会输出 '\r\r\n'。
 */

static void sf32lb52_send(struct uart_dev_s *dev, int ch)
{
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;
  uint32_t guard;

  /* 有界短等: 等 TXE (TDR 空), 不等 TC。240MHz 下 256 次空转约数 us,
   * 落在 "<10us" 的目标内; TXE 正常早已置位, 通常零次空转。 */

  guard = SF32LB52_SEND_TXE_MAX;

  while ((priv->usart->isr & USART_ISR_TXE) == 0 && guard-- != 0ul)
    {
    }

  priv->usart->tdr = (uint32_t)(ch & 0xff);
}

static const struct uart_ops_s g_sf32lb52_uart_ops =
{
  .setup       = sf32lb52_setup,
  .shutdown    = sf32lb52_shutdown,
  .attach      = sf32lb52_attach,
  .detach      = sf32lb52_detach,
  .ioctl       = sf32lb52_ioctl,
  .receive     = sf32lb52_receive,
  .rxavailable = sf32lb52_rxavailable,
  .rxint       = sf32lb52_rxint,
  .txready     = sf32lb52_txready,
  .txint       = sf32lb52_txint,
  .send        = sf32lb52_send,
};

/****************************************************************************
 * 中断处理
 ****************************************************************************/

static int sf32lb52_interrupt(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)arg;
  struct sf32lb52_uart_s *priv = (struct sf32lb52_uart_s *)dev->priv;
  struct sf32lb52_usart_s *u = priv->usart;
  struct sf32lb52_dmach_s *ch = &g_dmac->ch[SF32LB52_DMA_CH];
  uint32_t isr;
  uint32_t err = USART_ICR_ORECF | USART_ICR_FECF |
                 USART_ICR_PECF | USART_ICR_NCF;
  uint32_t dmac_isr;
  uint32_t total;
  uint32_t moved;
  uint32_t repump = 0;

  (void)irq;
  (void)context;

  /* 诊断: 进中断次数(USART1 与 DMAC1_CH7 合计)。 */

  g_rx_isr_count++;

  isr = u->isr;

  /* 【关键 1】ORE/FE/PE/NE 是"电平型 + 粘性"标志: 置位后只有写 ICR 才会清。
   * 若不清, 中断会被无限重复触发, CPU 被风暴占住, 后续字符在硬件层就被
   * 覆盖丢弃(实测一次按键后暴涨 24 次)。所以进中断第一时间清掉。
   * 这一条与 M1 已验证的行为完全一致, 保持不变。 */

  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_PE | USART_ISR_NE))
    {
      /* 诊断: ORE 增长 => 确有硬件覆盖(现在理论上不该再出现);
       *       FE 增长  => 采样/波特率不匹配。 */

      if (isr & USART_ISR_ORE)
        {
          g_rx_ore_count++;
        }

      if (isr & USART_ISR_FE)
        {
          g_rx_framing_err_count++;
        }

      u->icr = err;
    }

  /* 【关键 2】清 IDLE 与 DMAC1_CH7 的通道标志。
   *
   * IDLE 由 USART1 中断线报出; 半满/完成由 DMAC1_CH7 中断线报出。
   * 两条线共用本函数, 所以两边都检查、都清 —— 无论谁触发,
   * 后面的"提交 CNDTR -> 交给框架"都是同一段逻辑。
   *
   * 【必须清干净】IDLE 是粘性位(ICR.IDLECF), DMAC 的 GIF/TCIF/HTIF/TEIF
   * 是电平型(写 IFCR 清)。漏清任何一个都会让对应中断线一直拉高,
   * 表现为中断风暴、控制台被卡死。 */

  if (isr & USART_ISR_IDLE)
    {
      u->icr = USART_ICR_IDLECF;
    }

  dmac_isr = g_dmac->isr;

  if (dmac_isr & (DMAC_ISR_GIF(SF32LB52_DMA_CH)  |
                  DMAC_ISR_TCIF(SF32LB52_DMA_CH) |
                  DMAC_ISR_HTIF(SF32LB52_DMA_CH) |
                  DMAC_ISR_TEIF(SF32LB52_DMA_CH)))
    {
      g_dmac->ifcr = DMAC_ISR_GIF(SF32LB52_DMA_CH)  |
                     DMAC_ISR_TCIF(SF32LB52_DMA_CH) |
                     DMAC_ISR_HTIF(SF32LB52_DMA_CH) |
                     DMAC_ISR_TEIF(SF32LB52_DMA_CH);
    }

  /* 【关键 3】按厂家 last_index 游标提交"增量"。
   *
   * 唯一权威是厂家 drv_usart.c:407-413:
   *     recv_total_index = bufsz - __HAL_DMA_GET_COUNTER(...);
   *     if (recv_total_index < last_index)
   *         recv_len = bufsz + recv_total_index - last_index;   // 回绕
   *     else
   *         recv_len = recv_total_index - last_index;
   *     last_index = recv_total_index;
   * 即: total 是 0..bufsz 的【累计位置, 不掩码】; 每次只提交
   *     (本次 total - 上次 last) 这段增量; total 比 last 小说明跨过了
   *     缓冲区末尾, 用 bufsz + total - last 还原真实增量。
   *
   * 本驱动 g_rx_dma_last 就是厂家的 last_index; g_rx_dma_head/tail 改为
   * 【自由运行计数器】(不再是环形下标), 于是 head - tail 就是"已提交但
   * 框架还没取走"的准确字节数, 消除 head==tail 时空/满难分的歧义。
   *
   * dsb: DMA 写内存与写 CNDTR 之间需要屏障, 保证 CPU 后面读到的
   *      缓冲区内容确实是 CNDTR 已经统计过的那一段。 */

  total = SF32LB52_RX_DMA_SIZE - (ch->cndtr & 0xffffu);

  __asm__ __volatile__ ("dsb" ::: "memory");

  if (total < g_rx_dma_last)
    {
      moved = SF32LB52_RX_DMA_SIZE + total - g_rx_dma_last;
    }
  else
    {
      moved = total - g_rx_dma_last;
    }

  g_rx_dma_last  = total;
  g_rx_dma_head += moved;        /* 累计已提交字节数(自由运行) */
  g_rx_raw_read += moved;        /* 诊断: DMA 真实搬入的字节数增量 */

  /* 套圈检测: "已提交但未取走"超过一整圈, 说明这些字节已被 DMA 覆盖。
   * 只保留最近一圈, 其余计入 drop, 并让 tail 跳过被覆盖的字节。 */

  if ((g_rx_dma_head - g_rx_dma_tail) > SF32LB52_RX_DMA_SIZE)
    {
      uint32_t lost = (g_rx_dma_head - g_rx_dma_tail) - SF32LB52_RX_DMA_SIZE;

      g_rx_staged_drop += lost;
      g_rx_dma_tail    += lost;
    }

  /* 【关键 4】交给框架: uart_recvchars() 通过 receive() 把 DMA 环形缓冲里
   * 已搬入的字节搬进 NuttX 环形缓冲。
   *
   * 正常情况下 receive() 从缓冲直接取数, 第一趟就排空(repump 保持 0);
   * 只有框架的 recv 环形缓冲已满时才会"提前收手", 此时多跑也无益, 因此
   * 用 SF32LB52_RX_REPUMP_MAX 兜底, 绝不在中断里长自旋, 也不做任何打印。 */

  if (g_rx_dma_head != g_rx_dma_tail)
    {
      do
        {
          uart_recvchars(dev);

          if (g_rx_dma_head == g_rx_dma_tail)
            {
              break;
            }

          repump++;
        }
      while (repump < SF32LB52_RX_REPUMP_MAX);

      g_rx_stage_repump += repump;
    }

  /* TXE 在空闲时恒为 1, 必须同时判断 TXEIE, 否则白白多跑一次 */

  if ((isr & USART_ISR_TXE) != 0 &&
      (priv->usart->cr1 & USART_CR1_TXEIE) != 0)
    {
      uart_xmitchars(dev);
    }

  return OK;
}




/****************************************************************************
 * RX 字节级诊断打印 (M3)
 *
 * 【为什么用独立内核线程, 而不是在中断里打点】
 *   115200 下一个字符 87us; 在 ISR / 关中断窗口里 syslog 一行要几毫秒,
 *   必然把后续字符冲掉 —— 等于"用探针杀死被测对象"。所以计数只在 ISR
 *   里自增(几条指令), 打印交给一个低优先级内核线程。
 *
 * 【怎么用 / 怎么看】
 *   板级 bringup 调用 sf32lb52_rx_diag_start() 后, 该线程每秒醒来一次,
 *   只要计数任一变化就打印两行:
 *     [RXDIAG] isr=.. raw=.. fw=.. drop=.. ore=.. fe=..      (绝对计数)
 *     [RXDIAG] d-isr=.. d-raw=.. ... rep=..                  (本次增量)
 *   因此在 nsh> 下敲 help 回车, 约 1 秒内串口就会出现新的 RXDIAG 两行。
 *   计数不变时不重复打印, 不会刷屏。
 *
 * 【判据】对 help(4 个字符 + 回车)看新出现那两行的增量:
 *   d-raw >= 4 且 d-fw < 4 -> 字节已进驱动暂存区, 丢在"暂存区 -> 框架":
 *                            即 receive()/uart_recvchars() 或框架环形缓冲。
 *   d-raw <  4             -> 连 RDR 都没抢到, 丢在"中断/硬件"这一段:
 *                            中断进得不够, 或 RDR 被后到字节覆盖。
 *   d-ore 增长             -> 确有硬件溢出覆盖, 佐证 d-raw<4 的硬件解释。
 *   d-drop 增长            -> 32 字节暂存区被填满。
 *   d-fe  增长             -> 帧格式 / 波特率 / 采样不匹配。
 *   rep   增长(>0)         -> 单次中断里 uart_recvchars() 一趟没排空暂存区,
 *                            本次修复的"残留缺口"真实发生(已由循环兜住)。
 *   rep 恒为 0             -> 框架一趟就能排空, 残留缺口未发生;
 *                            此时若仍丢字节, 应看 d-raw / d-ore, 即丢在硬件侧。
 ****************************************************************************/

static int sf32lb52_rxdiag_thread(int argc, FAR char *argv[])
{
  uint32_t last_isr  = 0;
  uint32_t last_raw  = 0;
  uint32_t last_fw   = 0;
  uint32_t last_drop = 0;
  uint32_t last_ore  = 0;
  uint32_t last_fe   = 0;
  uint32_t last_rep  = 0;

  (void)argc;
  (void)argv;

  for (;;)
    {
      uint32_t isr;
      uint32_t raw;
      uint32_t fw;
      uint32_t drop;
      uint32_t ore;
      uint32_t fe;
      uint32_t rep;

      sleep(1);

      isr  = g_rx_isr_count;
      raw  = g_rx_raw_read;
      fw   = g_rx_to_framework;
      drop = g_rx_staged_drop;
      ore  = g_rx_ore_count;
      fe   = g_rx_framing_err_count;
      rep  = g_rx_stage_repump;

      if (isr != last_isr || raw != last_raw || fw != last_fw ||
          drop != last_drop || ore != last_ore || fe != last_fe ||
          rep != last_rep)
        {
          /* 增量必须在更新 last_* 之前算出来, 否则恒为 0 */

          uint32_t d_isr  = isr  - last_isr;
          uint32_t d_raw  = raw  - last_raw;
          uint32_t d_fw   = fw   - last_fw;
          uint32_t d_drop = drop - last_drop;
          uint32_t d_ore  = ore  - last_ore;
          uint32_t d_fe   = fe   - last_fe;

          last_isr  = isr;
          last_raw  = raw;
          last_fw   = fw;
          last_drop = drop;
          last_ore  = ore;
          last_fe   = fe;
          last_rep  = rep;

          syslog(LOG_INFO,
                 "[RXDIAG] isr=%lu raw=%lu fw=%lu drop=%lu ore=%lu fe=%lu\n",
                 (unsigned long)isr, (unsigned long)raw,
                 (unsigned long)fw,  (unsigned long)drop,
                 (unsigned long)ore, (unsigned long)fe);
          syslog(LOG_INFO,
                 "[RXDIAG] d-isr=%lu d-raw=%lu d-fw=%lu d-drop=%lu "
                 "d-ore=%lu d-fe=%lu rep=%lu\n",
                 (unsigned long)d_isr,  (unsigned long)d_raw,
                 (unsigned long)d_fw,   (unsigned long)d_drop,
                 (unsigned long)d_ore,  (unsigned long)d_fe,
                 (unsigned long)rep);
        }
    }

  return 0;
}

/****************************************************************************
 * Name: sf32lb52_rx_diag_start
 *
 * Description:
 *   启动 RX 字节级诊断线程。由板级 bringup 在控制台注册之后调用
 *   (见 boards/arm/sf32lb52/sf32lb52-lcd/src/sf32lb52_lcd_bringup.c)。
 *   内部幂等: 重复调用只会启一次。
 ****************************************************************************/

void sf32lb52_rx_diag_start(void)
{
  static bool diag_started = false;
  int pid;

  if (diag_started)
    {
      return;
    }

  /* 优先级 90 —— 低于 NSH 的默认 100 (NuttX 数值越大优先级越高),
   * 诊断线程不会去抢接收路径。 */

  pid = kthread_create("rxdiag", 90, 3072,
                       (main_t)sf32lb52_rxdiag_thread, NULL);

  if (pid < 0)
    {
      syslog(LOG_ERR, "[RXDIAG] kthread_create failed: %d\n", pid);
      return;
    }

  diag_started = true;
}

/****************************************************************************
 * Name: sf32lb52_rxdiag_dump
 *
 * Description:
 *   导出给外部调用的诊断打印 (供 NSH 内置命令 rx / rxd 使用): 一次性输出
 *   两行 —— 第一行是 6 个绝对计数 isr / raw / fw(交给框架) / drop / ore / fe,
 *   第二行是"距本函数上次被调用"的增量, 末尾附带 rep(残留重送次数)。
 *
 *   【为什么要有增量行】绝对计数是单调递增的, 两次调用之间只差几, 肉眼
 *   比对容易看错; 增量行一眼就能看出"这一批来了几个字节、交给了框架几个"。
 *
 *   本函数只读文件级 volatile 计数, 不触碰任何收发寄存器, 也不改收发逻辑。
 *   调用者 (NSH 任务) 处于进程上下文, 因此用 printf 直接写控制台是安全的。
 *   静态的"上次值"只被本函数读写(同一 NSH 任务上下文), 无需加锁。
 ****************************************************************************/

void sf32lb52_rxdiag_dump(void)
{
  static uint32_t last_isr;
  static uint32_t last_raw;
  static uint32_t last_fw;
  static uint32_t last_drop;
  static uint32_t last_ore;
  static uint32_t last_fe;

  uint32_t isr  = g_rx_isr_count;
  uint32_t raw  = g_rx_raw_read;
  uint32_t fw   = g_rx_to_framework;
  uint32_t drop = g_rx_staged_drop;
  uint32_t ore  = g_rx_ore_count;
  uint32_t fe   = g_rx_framing_err_count;

  uint32_t d_isr  = isr  - last_isr;
  uint32_t d_raw  = raw  - last_raw;
  uint32_t d_fw   = fw   - last_fw;
  uint32_t d_drop = drop - last_drop;
  uint32_t d_ore  = ore  - last_ore;
  uint32_t d_fe   = fe   - last_fe;

  last_isr  = isr;
  last_raw  = raw;
  last_fw   = fw;
  last_drop = drop;
  last_ore  = ore;
  last_fe   = fe;

  printf("[RXDIAG] isr=%lu raw=%lu fw=%lu drop=%lu ore=%lu fe=%lu\n",
         (unsigned long)isr,  (unsigned long)raw,
         (unsigned long)fw,   (unsigned long)drop,
         (unsigned long)ore,  (unsigned long)fe);
  printf("[RXDIAG] d-isr=%lu d-raw=%lu d-fw=%lu d-drop=%lu "
         "d-ore=%lu d-fe=%lu rep=%lu\n",
         (unsigned long)d_isr,  (unsigned long)d_raw,
         (unsigned long)d_fw,   (unsigned long)d_drop,
         (unsigned long)d_ore,  (unsigned long)d_fe,
         (unsigned long)g_rx_stage_repump);
}

/****************************************************************************
 * 缓冲区与设备实例
 ****************************************************************************/

#ifdef CONFIG_SF32LB52_UART1_RXBUFSIZE
#  define CONSOLE_RXBUFSIZE CONFIG_SF32LB52_UART1_RXBUFSIZE
#else
#  define CONSOLE_RXBUFSIZE 128
#endif

#ifdef CONFIG_SF32LB52_UART1_TXBUFSIZE
#  define CONSOLE_TXBUFSIZE CONFIG_SF32LB52_UART1_TXBUFSIZE
#else
#  define CONSOLE_TXBUFSIZE 256
#endif

static char g_usart1_rxbuffer[CONSOLE_RXBUFSIZE];
static char g_usart1_txbuffer[CONSOLE_TXBUFSIZE];

static struct uart_dev_s g_usart1_dev =
{
  .ops    = &g_sf32lb52_uart_ops,
  .priv   = &g_usart1_priv,

  /* 【M2】让框架负责 '\n' -> '\r\n':
   *
   * 本设备没有设 isconsole (见下), 所以 uart_register() 不会替我们设
   * tc_oflag; 必须在这里显式开 OPOST|ONLCR。
   *
   * 好处: '\r' 由 uart_write()/uart_irqwrite() 插入到发送环形缓冲, 属于
   * "普通内存操作"; 驱动 sf32lb52_send() 因此不必在关中断的临界区里等 TXE
   * 才能补 CR —— 这正是把最长临界区从 ~87us 压到几个周期的关键。
   *
   * 注意: 一旦这里开了 ONLCR, sf32lb52_send() 里就【不能】再自己补 '\r',
   *       否则会输出 '\r\r\n'。
   *
   * 另: 早期控制台 (清 .bss 之前) 走的是 lowputc.c 的 arm_lowputc(), 那条路
   *     不经过本框架, 换行仍由它自己处理, 互不影响。
   */

  .tc_oflag = OPOST | ONLCR,

  .recv   =
  {
    .size   = CONSOLE_RXBUFSIZE,
    .buffer = g_usart1_rxbuffer,
  },
  .xmit   =
  {
    .size   = CONSOLE_TXBUFSIZE,
    .buffer = g_usart1_txbuffer,
  },
};

static struct uart_dev_s *g_uart_devs[1] =
{
  &g_usart1_dev,
};

/****************************************************************************
 * Name: up_earlyserialinit
 *
 * Description:
 *   早期串口初始化 — 由 arm_boot() 在 up_irqinitialize() 之前调用。
 *   此时中断尚未使能, 仅做硬件初始化 (引脚复用 + 波特率 + UE/TE/RE),
 *   为 arm_lowputc() 的轮询输出做好准备。
 *
 *   同时调用 sf32lb52_clockconfig() 使能外设时钟 (GPIO/LCDC/GPTIM 等),
 *   USART1 本身无需 ENR 使能 (默认调试口)。
 ****************************************************************************/

void up_earlyserialinit(void)
{
  extern void sf32lb52_clockconfig(void);
  extern void sf32lb52_lowsetup(void);

  /* 1. 使能外设时钟 (不含 USART1, 它为默认调试口)。
   *    __start() 已调用过, 此处重复调用是幂等的 (仅按位或)。 */

  sf32lb52_clockconfig();

  /* 2. USART1 引脚复用 + 波特率 + 使能。
   *    与 __start() 中使用的同一入口 (sf32lb52_lowsetup), 保证行为一致。 */

#ifdef CONFIG_ARCH_LOWPUTC
  sf32lb52_lowsetup();
#endif
}

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   注册控制台串口设备。由 arm_boot() 在 up_timer_initialize() 之后调用。
 *   硬件已在 up_earlyserialinit() 中初始化完毕, 此处仅注册设备节点。
 ****************************************************************************/

void arm_serialinit(void)
{
  /* 幂等 + 可重试:
   * 不同 NuttX 版本可能分别调用 arm_serialinit() / up_serialinit(),
   * 甚至在文件系统就绪之前就调用一次 (此时 register_driver 会失败)。
   * 因此只在注册成功后才置标志, 允许 board_late_initialize() 阶段重试。
   */

  static bool serial_registered = false;

  if (!serial_registered && g_uart_devs[0] != NULL)
    {
      if (uart_register(SF32LB52_CONSOLE_DEV, g_uart_devs[0]) == OK)
        {
          serial_registered = true;
        }
    }
}

/****************************************************************************
 * Name: up_serialinit
 *
 * Description:
 *   兼容别名。部分 openvela/NuttX 版本通过 up_serialinit() 调用串口初始化。
 ****************************************************************************/

void up_serialinit(void)
{
  arm_serialinit();
}