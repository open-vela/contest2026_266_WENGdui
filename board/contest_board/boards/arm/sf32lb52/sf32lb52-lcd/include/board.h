/**
 * @file board.h
 * @brief SF32LB52-LCD 板级定义 (openvela / NuttX)
 */

#ifndef __BOARDS_ARM_SF32LB52_SF32LB52_LCD_INCLUDE_BOARD_H
#define __BOARDS_ARM_SF32LB52_SF32LB52_LCD_INCLUDE_BOARD_H

#include <nuttx/config.h>

/* ---- 时钟 ----
 * HCPU 主频 240MHz (由厂家 bootloader 设置 PLL)
 * USART 波特率时钟 = 48MHz 固定 (SystemFixClock, 非 HCLK)
 * SysTick 时钟源 = HCLK = 240MHz
 */

#define BOARD_XTAL_FREQ        48000000ul
#define BOARD_SYSTEM_CLOCK     240000000ul

/* ---- 系统时基 ----
 * openvela 公共层 (arm_m/arm_systick.c) 用该频率配置 SysTick。
 * 传入 true 表示 SysTick 以处理器时钟为源, 故此处取 HCPU 主频。
 */

#define BOARD_SYSTICK_CLOCK    BOARD_SYSTEM_CLOCK

/* ---- Flash (QSPI2 NOR, XIP) ----
 * 应用镜像烧录在 ER_IROM1 分区: 0x12218000, 上限 0x240000 (2.25MB)。
 * 权威来源: 厂家 link_copy.lds / ftab.c / ptab.h
 */

#define BOARD_FLASH_BASE       0x12218000ul
#define BOARD_FLASH_SIZE       (0x240000)

/* ---- SRAM ---- */

#define BOARD_SRAM_BASE        0x20000000ul
#define BOARD_SRAM_SIZE        (512 * 1024)
#define BOARD_SRAM_END         (BOARD_SRAM_BASE + BOARD_SRAM_SIZE)

/* mailbox 区在 SRAM 顶端 1KB */
#define BOARD_MBOX_BASE        (BOARD_SRAM_END - 0x400)

/* ---- 显示 (QSPI 面板) ----
 * 分辨率需与屏体实际规格核对后再定稿
 */

#define BOARD_LCD_WIDTH        390
#define BOARD_LCD_HEIGHT       450
#define BOARD_LCD_USE_QSPI     1

/* ---- NTC ADC ---- */

#define BOARD_NTC_CHAMBER_CH   0
#define BOARD_NTC_AMBIENT_CH   1

/* ---- 控制 ---- */

#define BOARD_HEATER_PIN       33
#define BOARD_FAN_PWM_DEV      "/dev/pwm2"
#define BOARD_FAN_PWM_CH       1
#define BOARD_FAULT_LED_PIN    26

/* ---- 按键 ---- */

#define BOARD_KEY_POWER_PIN    34
#define BOARD_KEY_SET_PIN      11

/* ---- 调试串口 USART1 ----
 *
 * 波特率 115200。
 *
 * 注意: 这个宏不是"仅供参考"的常量, 它会被真正写进 USART1 的 BRR 寄存器
 *       (不依赖 bootloader 遗留值), 写入点在:
 *         drivers/sf32lb52_lowputc.c : sf32lb52_lowsetup()
 *           -> sf32lb52_usart_setbaud(USART1, BOARD_CONSOLE_BAUD)
 *         arch/arm/src/sf32lb52/sf32lb52_usart.h : sf32lb52_usart_setbaud()
 *           brr = SystemFixClock / baud
 *
 * 分频基准是固定的 48MHz (与 HCLK 无关):
 *   sdk/drivers/Include/bf0_hal.h:225   #define SystemFixClock  48000000
 *   sdk/drivers/hal/bf0_hal_uart.c:288  brr = SystemFixClock / BaudRate
 * 即 115200 时 BRR = 48000000 / 115200 = 416 = 0x1A0
 * (>= BRR 下限 0x10, 走 16 倍过采样, 无需 OVER8)。
 *
 * 为什么从 1Mbps 降下来: 1Mbps 每字节只有 10us, 而 TX/换行那段关中断窗口
 * (见 sf32lb52_serial.c 对 sf32lb52_send 的说明) 曾长于一个字符时间,
 * 必然丢字符。115200 每字节 87us, 已把该窗口压到远小于 1 个字符时间。
 *
 * 注意: 若此处与上位机不一致, 表现为【完全无输出或乱码】
 *       (NuttX 在发, 但对方采样点全错)。
 *       另外厂家 bootloader 上电打印的 "SFBL" 仍是 1Mbps, 用 115200 看是乱码,
 *       属正常现象; 从应用自己的输出开始才是干净的 115200。
 */

#define BOARD_CONSOLE_BAUD     115200

#endif /* __BOARDS_ARM_SF32LB52_SF32LB52_LCD_INCLUDE_BOARD_H */