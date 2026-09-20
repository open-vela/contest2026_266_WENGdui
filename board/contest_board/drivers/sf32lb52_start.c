/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_start.c
 *
 * 芯片级复位入口 __start
 *
 * 参照实现: arch/arm/src/nrf53/nrf53_start.c
 *
 * 执行顺序 (不可调换):
 *   cpsid i                      关全局中断
 *   → sf32lb52_clockconfig()     使能外设时钟 (PLL 已由 bootloader 设好)
 *   → sf32lb52_lowsetup()        配置控制台引脚 + 波特率, 尽早出声
 *   → 清 .bss                    inline 循环, 不能调 memset
 *   → 搬 .data (flash→SRAM)      用 _eronly/_sdata/_edata
 *   → arm_fpuconfig()            (若启用 FPU)
 *   → up_earlyserialinit()       注册早期控制台
 *   → nx_start()                 NuttX 主入口
 *
 * 重要约束:
 *   在"清 .bss / 搬 .data"之前, 该函数只能调用不依赖已初始化全局变量的
 *   代码。lowputc.c 已为此把控制台基址改成宏 (编译期常量)。
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/init.h>
#include <nuttx/arch.h>
#include <arch/irq.h>
#include <arch/board/board.h>

#include "arm_internal.h"
#include "nvic.h"
#include "chip.h"
#include "sf32lb52_memorymap.h"
#include "sf32lb52_rcc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 进度标记: 本源码树中 showprogress() 未由公共头提供 (nrf53_start.c 能编译
 * 是因为它 include 了自己的 *_lowputc.h)。这里用 arm_lowputc() 等效替代。
 */

#ifndef showprogress
#  define showprogress(c) arm_lowputc(c)
#endif

/* 早期控制台 (由 serial.c 提供), 与 nrf53 的 nrf53_earlyserialinit 对应。
 * 该原型在公共头中已声明时, 此处重复声明不会冲突。
 */

#ifdef CONFIG_ARCH_EARLYSERIALINIT
void up_earlyserialinit(void);
#endif

/* 低层控制台初始化 (由 sf32lb52_lowputc.c 提供)。
 * 注意: 这些函数在"清 .bss / 搬 .data"之前被调用,
 *       只能依赖编译期常量, 不得引用已初始化的全局变量。
 */

void sf32lb52_lowsetup(void);
void sf32lb52_lowputc_initialize(uint32_t baud);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void __start(void) noinstrument_function;

/****************************************************************************
 * Name: sf32lb52_clockconfig
 *
 * Description:
 *   厂家 bootloader 已完成 PLL/时钟树初始化, 此处只使能所需外设时钟。
 *   本函数不得依赖已初始化的全局变量。
 ****************************************************************************/

void sf32lb52_clockconfig(void)
{
  uint32_t regval;

  regval  = SF32LB52_HPSYS_RCC->enr1;
  regval |= RCC_ENR1_PINMUX1;
  regval |= RCC_ENR1_LCDC1;
  regval |= RCC_ENR1_GPTIM1;
  regval |= RCC_ENR1_GPTIM2;
  regval |= RCC_ENR1_SPI1;
  regval |= RCC_ENR1_SPI2;
  SF32LB52_HPSYS_RCC->enr1 = regval;

  regval  = SF32LB52_HPSYS_RCC->enr2;
  regval |= RCC_ENR2_GPIO1;
  regval |= RCC_ENR2_MPI1;
  regval |= RCC_ENR2_MPI2;
  regval |= RCC_ENR2_GPADC;
  SF32LB52_HPSYS_RCC->enr2 = regval;
}

/****************************************************************************
 * 上板调试结论 (2026-09-15, 探针 probe.S ~ probe7.S)
 *
 * "烧录后只输出 SFBL" 的两个根因, 都已定位并修复:
 *
 *   1) ARMv8-M 的 MSPLIM (栈下限寄存器)
 *        ROM/bootloader 设过它; 而 NuttX 官方 arm_m/arm_head.S 里清 MSPLIM
 *        的那几行在本移植中没有编进去(我们的 __start 是 C 函数, 编译器在
 *        函数序言里就先 push, 于是第一条指令就踩线)。
 *        实测: 向量表 MSP=0x20002000 且不压栈 -> 一切正常;
 *              向量表 MSP=0x20001E48 且第一条 push -> 一个字符都没有;
 *              probe7 在 push 之前先清 MSPLIM/PSPLIM -> 全部正常,
 *              且 RAMSCAN 显示 0x20000000~0x20002000 逐字节可读写。
 *        修复: 见 boards/.../scripts/flash.ld 与 configs/thermo/defconfig,
 *              把 RAM 起点抬到 0x20002000, 使 .bss/空闲栈都在 MSPLIM 之上。
 *
 *   2) 覆盖了 ROM/bootloader 已经配好的 USART1
 *        完全不碰 USART 配置时, 控制台输出是干净的 1Mbps;
 *        一旦把 CR2/CR3/GTPR/MISCR 整寄存器清零就变二进制乱码。
 *        修复: sf32lb52_lowsetup() 改为纯读-改-写, 只置 TE/RE/UE。
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_start
 *
 * Description:
 *   C 启动主体。真正的复位入口是 arch/arm/src/sf32lb52/sf32lb52_head.S
 *   里的汇编 __start —— 它承担一件本函数【做不到】的事:
 *
 *     在【任何 push 之前】清 MSRLIM / PSPLIM 栈下限寄存器。
 *
 *   因为 C 函数的序言由编译器自动插入, 本函数一进来就已经 push 了,
 *   而 ROM/bootloader 把 MSPLIM 设在了应用栈所在的位置 (见 mem_map.h 的
 *   BOOTLOADER_RAM_DATA_END_ADDR = 0x20010000), 那一 push 会直接触发
 *   栈越界异常 -> 表现就是"串口只输出 SFBL"。
 *
 *   因此本函数改由 sf32lb52_head.S 的 __start 通过 b 跳入, 这里可以
 *   正常使用栈。
 ****************************************************************************/

void sf32lb52_start(void)
{
  const uint32_t *src;
  uint32_t *dest;

  /* 确保全局中断关闭 (复位后默认关闭, 但 bootloader 可能已打开) */

  __asm__ __volatile__ ("\tcpsid  i\n");

  /* ================= 上板探针 (临时, 定位"只输出 SFBL") =================
   *
   * 每个初始化步骤前后各打一个标记字符, 一步一个脚印:
   *
   *   '!'  自己的最低限度 USART1 初始化之后  -> 应用被执行了, 且控制台能用
   *   '1'  sf32lb52_clockconfig() 之后        -> RCC 写寄存器没炸
   *   '2'  sf32lb52_lowsetup() 之后           -> 引脚复用 + 波特率配置完成
   *   'A'~'G' 后面正常的启动进度
   *
   * 判读方法 (这次不依赖 ROM 留下的状态, 判据很硬):
   *   看到 '!' 1 2 A ...  -> 应用在跑, 控制台也没问题, 继续往后查
   *   看到 '!' 但没有 '1'   -> clockconfig() 里出事了
   *   看到 '!1' 没有 '2'    -> lowsetup()/pinmux 里出事了
   *   【一个字符都没有】     -> 应用根本没被执行 (或死在 arm_head.S 里,
   *                            还没进到 C 代码)
   */

  /* 【M1 阶段: 不调用 sf32lb52_clockconfig()】
   *
   *   ROM/bootloader 已经把需要的时钟都打开了, 我们再 OR 一遍 RCC
   *   ENR1/ENR2 既无必要, 又有风险 —— 实测表明: 在 showprogress('A')
   *   之前碰过 RCC / PINMUX 之后, 控制台输出会变成"每个字节最高位被置 1"
   *   的乱码 (剥掉 bit7 后能读出 "ABC" / "BOARD" / "LCD" / "console" 等
   *   真实文本, 证明数据本身是对的, 只是帧被破坏)。
   *
   *   什么都不碰时 (probe4 / probe7) 输出逐字节正确。
   *
   *   M2 需要 LCD / ADC / PWM 时再按厂家 HAL 的读-改-写方式逐个恢复。
   */

#if 0
  sf32lb52_clockconfig();
#endif

  /* 控制台: 只把 TE/RE/UE 置上, 不动引脚、不动波特率 */

  sf32lb52_lowsetup();

  showprogress('A');

  /* 清 .bss。此处内联循环而不调 memset, 以确保不依赖任何全局状态。 */

  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }

  showprogress('B');

  /* 把 .data 从 flash 中的暂存处 (_eronly, 紧跟只读数据之后)
   * 搬到 SRAM 中的运行地址 (_sdata.._edata)。
   */

  for (src  = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  showprogress('C');

#ifdef CONFIG_ARCH_FPU
  /* 初始化 FPU (若可用) */

  arm_fpuconfig();
#endif

  showprogress('D');

#ifdef CONFIG_ARCH_EARLYSERIALINIT
  /* 早期控制台初始化 (此时 .data/.bss 已就绪, serial.c 的全局变量可用) */

  up_earlyserialinit();
#endif

  showprogress('E');

#ifdef CONFIG_ARCH_PERF_EVENTS
  up_perf_init((void *)BOARD_SYSTICK_CLOCK);
#endif

  showprogress('\r');
  showprogress('\n');

  /* 交给 NuttX 内核。此后不再返回。
   * board_late_initialize() 由 nx_start() 依据
   * CONFIG_BOARD_LATE_INITIALIZE 自动调用。
   */

  /* ---- 【M1 关键修复】进入内核之前必须打开全局中断 ----
   *
   * 本移植的复位入口是自己写的汇编 stub (sf32lb52_head.S), 开头 cpsid i;
   * 而 NuttX 上游的 arm_head.S 会在调用 nx_start() 之前打开全局中断,
   * 这一步我们漏了。
   *
   * 已核对 nx_start.c 源码: nx_start() 内部【没有】up_irq_enable() 或
   * cpsie i, 所以开中断的责任在调用者 (即这里)。
   *
   * 漏掉的后果链:
   *   PRIMASK 一直为 1
   *     -> SysTick 虽已 arm 好, 中断却永远进不来 => 没有任何系统节拍
   *     -> 轮询输出(showprogress / syslog 回退 / up_putc)仍正常,
   *        所以 ABCDE / [BOARD] / [M1] 都能看到
   *     -> 但控制台的中断驱动发送 (printf -> write -> TXE 中断)
   *        会永久阻塞在信号量上 => NSH 横幅一个字都不出
   * 与全部实测现象一致。
   */

  __asm__ __volatile__ ("\tcpsie  i\n");

  nx_start();

  /* 不应到达此处 */

  for (; ; );
}