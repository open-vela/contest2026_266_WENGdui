/****************************************************************************
 * apps/thermo_panel/rxd_alias.c
 *
 * NSH 内置命令 'rxd' —— 只是 'rx' 的历史别名, 与 rxd_main.c 做同一件事:
 * 调用芯片串口驱动导出的 RX 诊断打印。
 *
 * 【为什么单独一个文件】NuttX 的 apps/Application.mk 按出现顺序把
 *   MAINSRC 与 PROGNAME 一一配对, 并用 -Dmain=<PROGNAME>_main 重命名入口;
 *   同一个 .c 登记两次会导致对象名冲突、-Dmain 互相覆盖, 因此别名必须
 *   有自己的源文件 (两行 main 而已)。
 *
 * 用法:
 *     nsh> rx        <- 主名字(2 字符, 丢尾巴也能命中)
 *     nsh> rxd       <- 本别名(兼容旧习惯)
 *
 * 输出 (两行):
 *     [RXDIAG] isr=.. raw=.. fw=.. drop=.. ore=.. fe=..
 *     [RXDIAG] d-isr=.. d-raw=.. d-fw=.. d-drop=.. d-ore=.. d-fe=.. rep=..
 ****************************************************************************/

#include <nuttx/config.h>

void sf32lb52_rxdiag_dump(void);

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  sf32lb52_rxdiag_dump();
  return 0;
}