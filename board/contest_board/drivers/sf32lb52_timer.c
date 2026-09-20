/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_timer.c
 *
 * 系统时基: 使用 Cortex-M33 内核 SysTick, 走 openvela(NuttX 12) 的
 *           公共 timer lower-half 框架。
 *
 * 参考实现: arch/arm/src/nrf53/nrf53_systick.c
 *
 * 关键依赖 (缺一不可):
 *   1. 公共实现 arch/arm/src/arm_m/arm_systick.c 提供
 *        struct timer_lowerhalf_s *systick_initialize(bool coreclk,
 *                                                     unsigned int freq,
 *                                                     int minor);
 *      其声明在 arch/arm/src/arm_m/systick.h, arm_m 已在 include 路径上。
 *   2. 芯片 Kconfig 必须 select ARMV8M_SYSTICK,
 *      否则 arm_m/Make.defs 不会把 arm_systick.c 加入 CMN_CSRCS,
 *      链接期会报 systick_initialize 未定义。
 *   3. 板级 include/board.h 必须定义 BOARD_SYSTICK_CLOCK。
 *
 * 注意: 本文件由 Make.defs 在 CONFIG_SF32LB52_SYSTIMER_SYSTICK=y 时挂载。
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <time.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/board/board.h>

#include <nuttx/timers/arch_timer.h>

#include "systick.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Function:  up_timer_initialize
 *
 * Description:
 *   启动过程中由 arm_initialize() 调用, 用于初始化系统时基中断。
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  /* coreclk = true 表示 SysTick 时钟源取处理器时钟,
   * 频率由板级 BOARD_SYSTICK_CLOCK 给出 (等于 HCPU 主频)。
   * minor = -1 表示不需要注册字符设备节点, 仅作系统时基。
   */

  up_timer_set_lowerhalf(systick_initialize(true, BOARD_SYSTICK_CLOCK, -1));
}