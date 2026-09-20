/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_irq.h
 *
 * 兼容封装: 中断号定义已移至 arch 公共头
 *   arch/arm/include/sf32lb52/irq.h
 * 该文件经 configure.sh 链接为 include/arch/chip/irq.h,
 * 由 <arch/irq.h> 间接包含, 并负责提供 NR_IRQS。
 *
 * 本文件保留原有的 SF32LB52_NR_IRQS 等别名, 供芯片层 .c 使用。
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_SF32LB52_SF32LB52_IRQ_H
#define __ARCH_ARM_SRC_SF32LB52_SF32LB52_IRQ_H

#include <arch/sf32lb52/irq.h>

/* 外设中断数 (NVIC 待关闭/待清除的中断线数) */

#define SF32LB52_NR_IRQS   SF32LB52_IRQ_NEXTINT

#endif /* __ARCH_ARM_SRC_SF32LB52_SF32LB52_IRQ_H */