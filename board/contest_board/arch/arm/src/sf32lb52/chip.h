/**
 * @file chip.h
 * @brief SiFli SF32LB52X 芯片级公共定义 (NuttX arch 层)
 *
 * 内核: Cortex-M33 (Armv8-M), FPU/DSP/MPU/VTOR, NVIC 3 位优先级
 */

#ifndef __ARCH_ARM_SRC_SF32LB52_CHIP_H
#define __ARCH_ARM_SRC_SF32LB52_CHIP_H

#include <nuttx/config.h>

#include <arch/irq.h>                 /* -> include/arch/chip/irq.h */
#include <arch/sf32lb52/chip.h>       /* NVIC 优先级等芯片公共宏 */

#include "sf32lb52_memorymap.h"
#include "sf32lb52_irq.h"

/* ---- 外设中断数 (供 arm_m 公共向量层使用) ----
 * 公共层依据该宏推导 NR_IRQS, 约定见 arch/arm/src/nrf53/chip.h。
 * 定义必须与 <arch/sf32lb52/irq.h> 中的 SF32LB52_IRQ_NEXTINT 一致。
 */

#define ARMV8M_PERIPHERAL_INTERRUPTS SF32LB52_IRQ_NEXTINT

/* 兼容别名 (重要)!
 *
 * arm_m/arm_vectors.c 与 arm_m/ram_vectors.h 是按下面这个顺序选宏名的:
 *   #ifdef  CONFIG_ARCH_ARMV6M -> ARMV6M_PERIPHERAL_INTERRUPTS
 *   #elif defined(CONFIG_ARCH_ARMV7M) -> ARMV7M_PERIPHERAL_INTERRUPTS
 *   #elif defined(CONFIG_ARCH_ARMV8M) -> ARMV8M_PERIPHERAL_INTERRUPTS
 *
 * 本源码树中 CONFIG_ARCH_ARMV7M 可能因残留符号而意外为 y
 * (见 defconfig 里 CONFIG_ARCH_CORTEXM3 的说明), 于是第一个命中的
 * 分支会去要 ARMV7M_PERIPHERAL_INTERRUPTS, 报:
 *   error: 'ARMV7M_PERIPHERAL_INTERRUPTS' undeclared here
 *   error: array index in initializer not of integer type
 *
 * 两个名字语义相同 (都是外设中断数), 因此直接提供别名。
 */

#define ARMV7M_PERIPHERAL_INTERRUPTS ARMV8M_PERIPHERAL_INTERRUPTS

/* ---- 内核参数 ---- */

#define SF32LB52_CORE_CM33           1
#define SF32LB52_NVIC_PRIO_BITS      3
#define SF32LB52_MPU_REGIONS         12
#define SF32LB52_HAS_FPU             1
#define SF32LB52_HAS_DSP             1

#define SF32LB52_NR_VECTORS          (16 + SF32LB52_NR_IRQS)

/* ---- 栈与堆 (SRAM 顶端留出 mailbox 区) ---- */

#define SF32LB52_IDLESTACK_TOP       (SF32LB52_MBOX_BUF_ADDR)
#define SF32LB52_IDLESTACK_SIZE      (4 * 1024)

#define SF32LB52_HEAP_BASE           SF32LB52_RAM2_BASE
#define SF32LB52_HEAP_SIZE           SF32LB52_RAM2_SIZE

/* ---- 系统时基 ----
 * SysTick 时钟源 = 处理器时钟。具体频率由板级 BOARD_SYSTEM_CLOCK 给出,
 * 时基初始化见 sf32lb52_timer.c。
 */

/* ---- 双核 ---- */

#define SF32LB52_HAVE_LCPU           1
#define SF32LB52_NUM_CPUS            2

#endif /* __ARCH_ARM_SRC_SF32LB52_CHIP_H */