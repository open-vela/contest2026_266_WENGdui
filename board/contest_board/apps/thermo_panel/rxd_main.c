/****************************************************************************
 * apps/thermo_panel/rxd_main.c
 *
 * NSH 内置命令 'rx' (历史别名 'rxd' 见 rxd_alias.c) —— 打印 SF32LB52
 * 控制台串口 (USART1) 的 RX 字节级诊断计数。
 *
 * 用法 (在 nsh> 提示符下):
 *     nsh> rx
 * 输出 (两行):
 *     [RXDIAG] isr=.. raw=.. fw=.. drop=.. ore=.. fe=..
 *     [RXDIAG] d-isr=.. d-raw=.. d-fw=.. d-drop=.. d-ore=.. d-fe=.. rep=..
 *
 * 字段含义:
 *   isr  = 进入 USART1 接收中断的次数
 *   raw  = 从 RDR 抢进驱动暂存区的字节数
 *   fw   = 经 receive() 交给 NuttX 串口框架的字节数
 *   drop = 暂存区满而未抢入的字节数
 *   ore  = 硬件接收溢出 (ORE) 次数
 *   fe   = 帧错误 (FE) 次数
 *   rep  = 单次中断内 uart_recvchars() 多跑一趟才排空暂存区的次数
 *          (>0 即"暂存区残留"缺口真实发生; 本次已用循环兜住)
 *
 * 第二行 d-* 是距上次调用本命令期间的增量, 便于对比:
 *   d-raw >= 4 且 d-fw < 4 -> 丢在 "暂存区 -> 框架"
 *   d-raw <  4             -> 丢在 "中断/硬件" (d-ore 增长可佐证)
 *
 * 说明:
 *   本文件在编译时由 apps/Application.mk 用 -Dmain=rx_main 把 main
 *   重命名, 并注册为 NSH 内置命令 (PROGNAME=rx)。
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>

/* 芯片层 (arch/arm/src/sf32lb52/sf32lb52_serial.c) 导出的诊断打印函数。
 * 非 static; 本工程为扁平构建 (CONFIG_BUILD_FLAT=y), libapps.a 与内核
 * 链接在同一地址空间, 直接调用即可。 */

void sf32lb52_rxdiag_dump(void);

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  sf32lb52_rxdiag_dump();
  return 0;
}