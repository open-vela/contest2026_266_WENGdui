/****************************************************************************
 * arch/arm/src/sf32lb52/sf32lb52_head.S
 *
 * SF32LB52X 复位入口 (汇编)。本文件提供全局符号 __start, 取代
 * NuttX 默认由 arm_head.S 提供的那个 —— 而 arm_head.S 在本移植里
 * 并没有被编入 CHIP_ASRCS。
 *
 * 【为什么必须自己写: MSPLIM 栈下限】
 *
 *   ARMv8-M 有 MSPLIM / PSPLIM 两个栈下限寄存器: 一旦压栈越过下限,
 *   立刻触发栈越界异常。
 *
 *   SiFli 的 ROM/bootloader 在把控制权交给应用之前设过 MSPLIM, 用来
 *   保护它自己占用的 RAM:
 *       BOOTLOADER_RAM_DATA_START_ADDR = 0x20000000
 *       BOOTLOADER_RAM_DATA_END_ADDR   = 0x20010000   (64KB)
 *
 *   NuttX 的 __start 是【C 函数】, 编译器在函数序言里就先 push ——
 *   于是整条链路上没有任何一处来得及清 MSPLIM, 应用的第一条指令就
 *   踩线。异常发生后 VTOR 仍指向 bootloader 的向量表, 结果是彻底静默。
 *
 *   上板实测 (probe5 / probe7, 见 openvela_thermo/probe_test/):
 *     向量表 MSP = 0x20001E48, 第一条 push          -> 一个字符都没有
 *     同一 MSP, 但 push 之前先清 MSPLIM / PSPLIM    -> 全部正常
 *     向量表 MSP 抬到 0x20003E48 (仍低于 0x20010000) -> 依然静默
 *   => 决定性的那一步是【清 MSPLIM】, 不是抬高 MSP。
 *
 * 【本文件的分工】
 *   __start (本文件, 汇编, 绝不压栈)
 *       -> cpsid i
 *       -> 清 MSPLIM / PSPLIM
 *       -> b sf32lb52_start   (C 主体, arch/arm/src/sf32lb52/sf32lb52_start.c)
 ****************************************************************************/

    .syntax unified
    .cpu    cortex-m33
    .thumb

    .section .text

    .thumb_func
    .global __start
    .type   __start, %function

__start:
    /* 1. 先关中断。
     *    此刻 VTOR 还指向 bootloader 的向量表, 任何异常进来都会走到
     *    不属于我们的处理程序, 必须挡住。
     */

    cpsid   i

    /* 2. 【关键】清栈下限。必须早于任何 push!
     *    MSPLIM 由 ROM/bootloader 设为保护它自己的 RAM 区, 不清掉的话
     *    应用第一次压栈就触发栈越界异常。
     */

    movs    r0, #0
    msr     MSPLIM, r0
    msr     PSPLIM, r0

    /* 3. 跳到 C 主体。用 b (尾跳) 而不是 bl:
     *    C 主体不会返回, 也不需要用到当前的栈帧。
     */

    b       sf32lb52_start

    .size   __start, . - __start
    .end