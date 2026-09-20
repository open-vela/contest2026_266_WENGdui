/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_lowputc.c
 *
 * 低层串口输出 (轮询模式控制台)
 *
 * 依据:
 *   USART 寄存器布局: cmsis/Include/usart.h
 *   波特率公式:       hal/bf0_hal_uart.c:288
 *                     brr = SystemFixClock / baud  (SystemFixClock = 48MHz)
 *
 * 重要约束:
 *   本文件在 __start() 中"清 .bss / 搬 .data 之前"就会被调用,
 *   因此【不得】依赖任何已初始化的全局/静态变量。
 *   控制台基址一律用宏 (编译期常量) 表示, 不用全局指针。
 *
 * ==========================================================================
 * M1 上板实测结论 (2026-09-15, 探针 probe.S ~ probe7.S 见 probe_test/)
 *
 *   A. ARMv8-M 的 MSPLIM 栈下限寄存器
 *        ROM/bootloader 用它保护自己的 64KB RAM
 *        (mem_map.h: BOOTLOADER_RAM_DATA_START_ADDR=0x20000000,
 *                    BOOTLOADER_RAM_DATA_END_ADDR  =0x20010000)。
 *        应用若要压栈, 必须在任何 push 之前清 MSPLIM —— 这一句由
 *        arch/arm/src/sf32lb52/sf32lb52_head.S 的汇编 __start 负责。
 *        (实测: 不清则第一条 push 即异常, 串口只出 SFBL)
 *
 *   B. USART1 的配置【不要覆盖】
 *        ROM/bootloader 已经把它配成 8 位/无校验/1 停止位/1Mbps,
 *        实测对照:
 *          一个外设寄存器都不写        -> 输出干净 (probe4 "A-ASIS")
 *          整寄存器清零 CR2/CR3        -> 乱码
 *          整寄存器写 PINR / pad       -> 乱码
 *        乱码已按字节验证: 收到 C1 C2 C3... 剥掉 bit7 得 41 42 43 = "ABC"
 *        (showprogress 的字母), 继续解出 "BOARD"/"SF"/"LCD"/"bringup"/
 *        "console"/"only" —— 数据本身正确, 是被我们的写操作破坏了帧格式。
 *
 *        厂家 HAL 全程只用 MODIFY_REG 改个别位, 从不清无关位,
 *        也【从不写 GTPR】。本文件严格遵守同样的约束。
 * ==========================================================================
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/board/board.h>

#include "chip.h"
#include "sf32lb52_memorymap.h"
#include "sf32lb52_rcc.h"
#include "sf32lb52_pinmux.h"
#include "sf32lb52_usart.h"
#include "arm_internal.h"

/* 寄存器结构与位定义统一来自 sf32lb52_usart.h (与 serial.c 共用) */

#define SF32LB52_CONSOLE_USART \
  ((struct sf32lb52_usart_s *)SF32LB52_USART1_BASE)

/****************************************************************************
 * Name: arm_lowputc
 *
 * 说明: NuttX ARM 通用层约定的低层字符输出接口。
 *
 * 重要: 等待 TXE 的循环必须是【有界】的。
 *   早期调试中若 USART1 因任何原因不响应 (未被使能/时钟异常/引脚异常),
 *   无界等待会让整个启动停在第一个字符上 —— 现象就是"串口什么都不出",
 *   且无法区分"应用没跑"和"控制台坏了"。
 *   这里加上限, 超时后照常返回, 保证启动流程一定能继续。
 ****************************************************************************/

#define SF32LB52_TXE_WAIT_MAX  400000ul

void arm_lowputc(char ch)
{
  uint32_t guard;

  /* 【换行顺序】遇到 \n 必须【先发 \r, 再发 \n】。
   *
   * 原实现是"先发 \n, 之后再补 \r", 后果:
   *   * 终端上多出一个空行;
   *   * 且下一行不回到第 0 列 —— M1 实测时 NSH 横幅与提示符挤在一起,
   *     输出 "NuttShell (NSH)nsh>" 而不是分成两行。
   */

  if (ch == '\n')
    {
      guard = SF32LB52_TXE_WAIT_MAX;

      while ((SF32LB52_CONSOLE_USART->isr & USART_ISR_TXE) == 0
             && guard-- != 0ul)
        {
        }

      SF32LB52_CONSOLE_USART->tdr = (uint32_t)'\r';
    }

  guard = SF32LB52_TXE_WAIT_MAX;

  while ((SF32LB52_CONSOLE_USART->isr & USART_ISR_TXE) == 0 && guard-- != 0ul)
    {
    }

  SF32LB52_CONSOLE_USART->tdr = (uint32_t)(uint8_t)ch;
}

/****************************************************************************
 * Name: up_putc
 ****************************************************************************/

#if defined(CONFIG_ARCH_LOWPUTC) || defined(CONFIG_DEBUG_FEATURES)
/* 注意: nuttx/arch.h:2892 的声明是 `void up_putc(int ch)`,
 * 返回类型必须是 void, 否则报 conflicting types。
 */

void up_putc(int ch)
{
  arm_lowputc((char)ch);
}
#endif

/****************************************************************************
 * Name: sf32lb52_pinmux_initialize
 *
 * 说明: 把 USART1 接到 PA18(RX) / PA19(TX)。
 *
 *   【M1 阶段本函数不被调用!】见 sf32lb52_lowsetup() 里的说明:
 *   实测写 PINR 与整寄存器写 pad 都会破坏 ROM 已配好的控制台。
 *   保留代码供 M2 使用, 但届时必须改成纯读-改-写并上板验证。
 *
 *   52X 与 55X 不同, 串口引脚需要两步:
 *     1) USART1_PINR 里登记这两根 pad 的序号 (pad - PAD_PA00)
 *     2) pad 自身的 FSEL 选到 "I2C_UART" 功能组 (FSEL=4)
 *
 *   依据: sdk/customer/boards/sf32lb52-lcd_base/bsp_pinmux.c
 *           HAL_PIN_Set(PAD_PA18, USART1_RXD, PIN_PULLUP, 1);
 *           HAL_PIN_Set(PAD_PA19, USART1_TXD, PIN_PULLUP, 1);
 ****************************************************************************/

void sf32lb52_pinmux_initialize(void)
{
  uint32_t regval;

  /* 1. 使能 PINMUX1 时钟 (HPSYS RCC ENR1 bit2) */

  regval  = SF32LB52_HPSYS_RCC->enr1;
  regval |= RCC_ENR1_PINMUX1;
  SF32LB52_HPSYS_RCC->enr1 = regval;

  /* 2. USART1_PINR 登记 RX/TX 所在 pad 序号
   *    字段值 = pad - PAD_PA00  (PA18 -> 18, PA19 -> 19)
   */

  regval  = getreg32(SF32LB52_HPSYS_CFG_USART1_PINR);
  regval &= ~(PINR_RXD_PIN_MASK | PINR_TXD_PIN_MASK);
  regval |= (uint32_t)(SF32LB52_UART1_RX_PAD - PAD_PA00) << PINR_RXD_PIN_SHIFT;
  regval |= (uint32_t)(SF32LB52_UART1_TX_PAD - PAD_PA00) << PINR_TXD_PIN_SHIFT;
  putreg32(regval, SF32LB52_HPSYS_CFG_USART1_PINR);

  /* 3. 两个 pad: FSEL=I2C_UART(4), 上拉, 输入使能
   *
   * 【注意】下面是整寄存器赋值, 会清掉 ROM 设的其它位 ——
   *         这正是 M1 阶段不能调用本函数的原因。
   *         M2 启用时应改为读-改-写, 并且只动 FSEL 字段。
   */

  regval = PINMUX_FSEL_USART1 | PINMUX_PULLUP | PINMUX_IE;

  putreg32(regval, SF32LB52_PAD_ADDR(SF32LB52_UART1_RX_PAD));
  putreg32(regval, SF32LB52_PAD_ADDR(SF32LB52_UART1_TX_PAD));
}

/****************************************************************************
 * Name: sf32lb52_lowsetup
 *
 * 说明: 复位后最早的控制台初始化入口, 由 sf32lb52_start() 在清 .bss 之前
 *       调用 (对应 nrf53 端口的 nrf53_lowsetup())。
 *
 *   【M2 现状: 做三件事, 全是读-改-写, 从不清无关位】
 *
 *     1) 读-改-写把 CR1 的 TE/RE/UE 置上;
 *     2) 用 sf32lb52_usart_setbaud() 写 BRR (控制台波特率以 BOARD_CONSOLE_BAUD
 *        为准, 不再沿用 bootloader 遗留的 1Mbps) —— BRR 只是分频值, 不影响帧格式;
 *     3) setbaud 内部只改 CR1 的 UE/TE/RE/OVER8 与 MISCR 的 SMPLINI 字段。
 *
 *   【绝对不做的事情】(每一项都实测导致过乱码或静默)
 *     * CR2 / CR3 / GTPR / MISCR 整寄存器赋值
 *         ROM 已配好 8 位/无校验/1 停止位, 清零 CR2(含 STOP/LINEN/
 *         CLKEN)、CR3 等于把它推翻 -> 乱码。
 *     * 整寄存器写 CR1 (清零)
 *         推翻 ROM 配好的帧格式 -> 乱码; 只允许按位读-改-写。
 *     * sf32lb52_pinmux_initialize()
 *         其中的 pad 寄存器是整寄存器写, 会清掉 ROM 设的其它位 -> 乱码。
 *
 *   实测依据: probe4 / probe7 —— 完全不动外设寄存器时, 控制台输出是
 *   逐字节正确的 1Mbps ASCII。
 ****************************************************************************/

void sf32lb52_lowsetup(void)
{
  uint32_t regval;

  regval  = SF32LB52_CONSOLE_USART->cr1;
  regval |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
  SF32LB52_CONSOLE_USART->cr1 = regval;

  /* 设置控制台波特率。
   *
   * BRR 只是分频值, 改它【不影响帧格式】; CR1/CR2/CR3/MISCR 一律不动,
   * ROM 配好的 8-N-1 原样保留。
   *
   * 为什么从 1Mbps 降到 115200:
   *   1Mbps => 每字节 10us, 而"中断->搬数据->唤醒读任务->上下文切换->
   *   回中断"这条回路超过 10us => 每个字符都在硬件层溢出(实测 4 个字符
   *   只读到 2 个, 且读到的都带 ORE)。115200 => 每字节 87us, 余量 8 倍。
   */

  sf32lb52_usart_setbaud(SF32LB52_CONSOLE_USART, BOARD_CONSOLE_BAUD);
}

/****************************************************************************
 * Name: sf32lb52_lowputc_initialize
 *
 * 兼容旧调用点 (serial.c / 早期调试): 允许指定波特率。
 *
 * 注意: 这里的 setbaud 是【读-改-写】, 只动 UE/TE/RE/OVER8 与 MISCR 的
 *       SMPLINI 字段, 不碰 CR2/CR3/GTPR, 也不动引脚。
 *       若 ROM 已把波特率配好, 通常不需要调用本函数。
 ****************************************************************************/

void sf32lb52_lowputc_initialize(uint32_t baud)
{
  sf32lb52_usart_setbaud(SF32LB52_CONSOLE_USART, baud);
}