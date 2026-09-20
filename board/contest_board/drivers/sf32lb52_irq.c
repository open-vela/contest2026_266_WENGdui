/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_irq.c
 *
 * NVIC 初始化
 *
 * 参照实现: arch/arm/src/nrf53/nrf53_irq.c (up_irqinitialize)
 *
 * 说明:
 *   向量表与异常分发由公共层提供 (arm_m/arm_vectors.c, arm_doirq.c),
 *   芯片层只负责: 关闭外设中断 / 设置向量表基址 / 优先级 / 使能内核异常。
 *
 *   中断使能 (up_enable_irq)、优先级 (up_prioritize_irq) 等由公共层提供,
 *   芯片层不重复实现 (与 nrf53 一致)。
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <arch/irq.h>

#include "chip.h"
#include "nvic.h"
#include "ram_vectors.h"
#include "arm_internal.h"
#include "sf32lb52_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 4 个字节拼成一个 32 位默认优先级 */

#define DEFPRIORITY32 \
  (NVIC_SYSH_PRIORITY_DEFAULT << 24 | NVIC_SYSH_PRIORITY_DEFAULT << 16 | \
   NVIC_SYSH_PRIORITY_DEFAULT << 8  | NVIC_SYSH_PRIORITY_DEFAULT)

/* SHCSR (System Handler Control and State) —— ARMv8-M 架构固定地址 */

#define SCB_SHCSR           0xE000ED24ul

#define SHCSR_MEMFAULTENA   (1ul << 16)
#define SHCSR_BUSFAULTENA   (1ul << 17)
#define SHCSR_USGFAULTENA   (1ul << 18)

/* 由 NVIC ENABLE 寄存器地址推算对应的 CLEAR ENABLE 寄存器偏移 */

#define NVIC_ENA_OFFSET     (0)
#define NVIC_CLRENA_OFFSET  (NVIC_IRQ0_31_CLEAR - NVIC_IRQ0_31_ENABLE)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   由 NuttX 启动流程调用, 负责:
 *     1. 关闭全部外设中断 (bootloader 可能已使能某些中断)
 *     2. 设置向量表基址 (XIP 启动的关键)
 *     3. 全部中断/异常设为默认优先级
 *     4. 使能 MemManage/BusFault/UsageFault
 *     5. 中断栈着色 (若启用独立中断栈)
 ****************************************************************************/

void up_irqinitialize(void)
{
  uint32_t regaddr;
  uint32_t regval;
  int num_priority_registers;
  int i;

  /* 1. 关闭全部外设中断 */

  for (i = 0; i < SF32LB52_IRQ_NEXTINT; i += 32)
    {
      putreg32(0xfffffffful, NVIC_IRQ_CLEAR(i));
    }

  /* 2. 设置向量表基址。
   *
   *    本板从 QSPI2 NOR (0x12010000) XIP 启动, 复位后 VTOR 仍指向
   *    bootloader 的向量表。必须显式改为 NuttX 自己的 _vectors,
   *    否则任何中断/异常都会跳到错误地址 (表现为 HardFault)。
   */

  putreg32((uint32_t)_vectors, NVIC_VECTAB);

#ifdef CONFIG_ARCH_RAMVECTORS
  /* 使用 RAM 向量表时需要额外初始化 */

  arm_ramvec_initialize();
#endif

  /* 3. 系统异常 (SYSH) 设为默认优先级 */

  putreg32(DEFPRIORITY32, NVIC_SYSH4_7_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH8_11_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH12_15_PRIORITY);

  /* NVIC ICTR 的 bit[4:0] 给出实现的中断线组数:
   *   0 -> 32 线, 1 -> 64 线, 2 -> 96 线 ... 每组 8 个优先级寄存器
   */

  num_priority_registers = (getreg32(NVIC_ICTR) + 1) * 8;

  regaddr = NVIC_IRQ0_3_PRIORITY;

  for (i = 0; i < num_priority_registers; i++)
    {
      putreg32(DEFPRIORITY32, regaddr);
      regaddr += 4;
    }

  /* 4. 使能三个可配置系统异常 (MemManage / BusFault / UsageFault) */

  regval  = getreg32(SCB_SHCSR);
  regval |= (SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA);
  putreg32(regval, SCB_SHCSR);

  /* 5. 中断栈着色 (仅在启用独立中断栈时有效) */

#ifdef CONFIG_ARCH_INTERRUPTSTACK
  arm_color_intstack();
#endif

  /* 此处不打开全局中断; 由 NuttX 启动流程统一负责 */
}

/****************************************************************************
 * Name: sf32lb52_irqinfo
 *
 * Description:
 *   给定 IRQ 号, 给出使能/禁止该中断所需的寄存器地址与位掩码。
 *   参考实现: arch/arm/src/nrf53/nrf53_irq.c: nrf53_irqinfo()
 *
 *   外设中断 -> NVIC 的 SETENA / CLRENA 寄存器
 *   系统异常 -> SYSHCON (MemManage/BusFault/UsageFault) 或 SysTick 控制寄存器
 ****************************************************************************/

static int sf32lb52_irqinfo(int irq, uintptr_t *regaddr, uint32_t *bit,
                            uintptr_t offset)
{
  int n;

  DEBUGASSERT(irq >= NVIC_IRQ_NMI && irq < NR_IRQS);

  /* 外设中断 */

  if (irq >= SF32LB52_IRQ_EXTINT)
    {
      n        = irq - SF32LB52_IRQ_EXTINT;
      *regaddr = NVIC_IRQ_ENABLE(n) + offset;
      *bit     = (uint32_t)1 << (n & 0x1f);
    }

  /* 系统异常: 只有少数可单独使能/禁止 */

  else
    {
      *regaddr = NVIC_SYSHCON;

      if (irq == NVIC_IRQ_MEMFAULT)
        {
          *bit = NVIC_SYSHCON_MEMFAULTENA;
        }
      else if (irq == NVIC_IRQ_BUSFAULT)
        {
          *bit = NVIC_SYSHCON_BUSFAULTENA;
        }
      else if (irq == NVIC_IRQ_USAGEFAULT)
        {
          *bit = NVIC_SYSHCON_USGFAULTENA;
        }
      else if (irq == NVIC_IRQ_SYSTICK)
        {
          *regaddr = NVIC_SYSTICK_CTRL;
          *bit     = NVIC_SYSTICK_CTRL_ENABLE;
        }
      else
        {
          return ERROR;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   禁止指定 IRQ。外设中断写 NVIC CLRENA (写 1 清除使能位);
 *   系统异常则读-改-写 SYSHCON/SysTick 控制寄存器。
 ****************************************************************************/

void up_disable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t regval;
  uint32_t bit;

  if (sf32lb52_irqinfo(irq, &regaddr, &bit, NVIC_CLRENA_OFFSET) == 0)
    {
      if (irq >= SF32LB52_IRQ_EXTINT)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          regval  = getreg32(regaddr);
          regval &= ~bit;
          putreg32(regval, regaddr);
        }
    }
}

/****************************************************************************
 * Name: up_ack_irq
 *
 * Description:
 *   应答 (acknowledge) 一个中断 —— 清除 NVIC 中该中断的 pending 位。
 *
 *   arm_doirq.c 会调用本函数:
 *     arch/arm/src/armv8-m/arm_doirq.c:62   arm_ack_irq(irq);
 *     arch/arm/src/armv8-m/arm_doirq.c:100  arm_ack_irq(irq);
 *   而声明见 arch/arm/src/common/arm_internal.h:306:
 *     #if defined(CONFIG_ARCH_ARMV7A) || ... ARMV7R || ... ARMV8R
 *     #  define arm_ack_irq(i)          <- 仅这几种架构定义为空宏
 *     #else
 *     void arm_ack_irq(int irq);      <- ARMv7-M / ARMv8-M 需芯片层实现
 *     #endif
 *
 *   因此这是芯片层职责, 缺少会导致链接期:
 *     undefined reference to `arm_ack_irq'
 *
 *   ARMv8-M 的 NVIC 在进入中断时会自动清 pending, 但显式清除是安全的,
 *   且与 nrf53 的做法一致 (nrf53_clrpend), 可避免边沿触发中断的重复挂起。
 ****************************************************************************/

void arm_ack_irq(int irq)
{
  /* ARMv8-M (Cortex-M33) 的 NVIC 在异常入口由硬件自动清挂起位,
   * 不需要任何"确认"操作。
   *
   * !! 千万不要在这里写 NVIC 寄存器 !! (踩过的坑)
   * 之前这里写的是:
   *     putreg32(1 << (n & 31), NVIC_IRQ_CLRPEND(n));
   * 而此源码树里 NVIC_IRQ_CLRPEND 实际落在偏移 0x180 —— 那是
   * ICER(清使能), 不是 ICPR(清挂起, 0x280)。arm_doirq() 每次
   * 中断都会调用 arm_ack_irq(), 于是【第一次中断就把这条 NVIC
   * 线自己关掉】, 之后该外设再也不产生中断。
   *
   * 症状: UART1 的 RX 只收到第一个字符 (RXNE 再高也不触发中断),
   *       而 TX 看着正常(短字符串塞得进发送缓冲, 不需要 TX 中断)。
   *
   * 保持空实现即可。
   */

  (void)irq;
}


/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   使能指定 IRQ。外设中断写 NVIC SETENA (写 1 置使能位);
 *   系统异常则读-改-写 SYSHCON/SysTick 控制寄存器。
 ****************************************************************************/

void up_enable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t regval;
  uint32_t bit;

  if (sf32lb52_irqinfo(irq, &regaddr, &bit, NVIC_ENA_OFFSET) == 0)
    {
      if (irq >= SF32LB52_IRQ_EXTINT)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          regval  = getreg32(regaddr);
          regval |= bit;
          putreg32(regval, regaddr);
        }
    }
}