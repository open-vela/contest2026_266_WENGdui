/**
 * @file sf32lb52_memorymap.h
 * @brief SiFli SF32LB52X 内存与外设地址映射
 *
 * 数据来源: 源码/xiaozhi-sf32-1.4.0/sdk/drivers/cmsis/sf32lb52x/
 *           mem_map.h / register.h
 */

#ifndef __ARCH_ARM_SRC_SF32LB52_SF32LB52_MEMORYMAP_H
#define __ARCH_ARM_SRC_SF32LB52_SF32LB52_MEMORYMAP_H

/* ---- 片内 ROM / SRAM ---- */

#define SF32LB52_HPSYS_ROM_BASE      0x00000000ul
#define SF32LB52_HPSYS_ROM_SIZE      (64 * 1024)

#define SF32LB52_RAM0_BASE           0x20000000ul   /* DTCM, 兼 retention */
#define SF32LB52_RAM0_SIZE           (128 * 1024)
#define SF32LB52_RAM1_BASE           0x20020000ul
#define SF32LB52_RAM1_SIZE           (128 * 1024)
#define SF32LB52_RAM2_BASE           0x20040000ul
#define SF32LB52_RAM2_SIZE           (256 * 1024)

#define SF32LB52_SRAM_BASE           SF32LB52_RAM0_BASE
#define SF32LB52_SRAM_SIZE           (512 * 1024)
#define SF32LB52_SRAM_END            (SF32LB52_SRAM_BASE + SF32LB52_SRAM_SIZE - 1)

#define SF32LB52_LPSYS_RAM_BASE      0x20400000ul
#define SF32LB52_LPSYS_RAM_SIZE      (24 * 1024)
#define SF32LB52_LPSYS_EM_BASE       0x20408000ul
#define SF32LB52_LPSYS_EM_SIZE       (24 * 1024)

/* ---- Mailbox / 自定义配置区 (位于 SRAM 顶端) ---- */

#define SF32LB52_MBOX_BUF_SIZE       (2 * 512)
#define SF32LB52_MBOX_BUF_ADDR       (SF32LB52_SRAM_END + 1 - SF32LB52_MBOX_BUF_SIZE)
#define SF32LB52_CUSTOMCFG_ADDR      0x2007fb04ul
#define SF32LB52_CUSTOMCFG_SIZE      256

/* ---- 片外 NOR / PSRAM ---- */

#define SF32LB52_QSPI1_MEM_BASE      0x10000000ul   /* NOR XIP */
#define SF32LB52_QSPI1_MEM_SIZE      (0x2000000)
#define SF32LB52_QSPI2_MEM_BASE      0x12000000ul   /* 启动 NOR (16MB) */
#define SF32LB52_QSPI2_MEM_SIZE      (0x1000000)
#define SF32LB52_PSRAM_CODE_BASE     0x60000000ul   /* PSRAM 代码区 (2MB) */
#define SF32LB52_PSRAM_CODE_SIZE     (0x200000)
#define SF32LB52_PSRAM_BASE          0x60200000ul   /* PSRAM 数据区 (6MB) */
#define SF32LB52_PSRAM_SIZE          (0x600000)

/* ---- NOR 启动布局 (由厂家 bootloader + ftab 决定, 不可更改) ----
 *
 * 权威来源:
 *   源码/xiaozhi-sf32-1.4.0/app/project/build_sf32lb52-lcd_n16r8_hcpu/
 *     link_copy.lds   (__ROM_BASE = 0x12218000, __ROM_SIZE = 0x240000)
 *     ftab/board/ftab.c (.ftab[4] = {base=0x12218000, size=0x240000})
 *     ptab.h          (HCPU_FLASH_CODE_START_ADDR = 0x12218000)
 *   devkit_lcd_n16r8_firmware/sftool_param.json
 *
 * 注意: 启动 flash 是 QSPI2 (0x12000000), 不是 QSPI1 (0x10000000)。
 */

#define SF32LB52_FLASH_TABLE_ADDR     0x12000000ul   /* ftab.bin */
#define SF32LB52_FLASH_TABLE_SIZE     (20 * 1024)
#define SF32LB52_FLASH_BOOTLOADER_ADDR 0x12208000ul  /* bootloader.bin */
#define SF32LB52_FLASH_BOOTLOADER_SIZE (1024 * 1024)

/* 应用程序 (ER_IROM1 分区): NuttX 镜像 nubttx.bin 烧录于此 */

#define SF32LB52_FLASH_USERCODE_ADDR  0x12218000ul
#define SF32LB52_FLASH_USERCODE_SIZE  (0x240000)     /* 2.25MB */

/* 厂家保留的其它分区 (本端口暂不使用) */

#define SF32LB52_FLASH_EZIP_ADDR      0x12460000ul   /* ER_IROM3, 0x680000 */
#define SF32LB52_FLASH_FONT_ADDR      0x12AE0000ul   /* ER_IROM2, 0x400000 */

/* ---- HPSYS 外设 ---- */

#define SF32LB52_HPSYS_RCC_BASE      0x50000000ul
#define SF32LB52_EXTDMA_BASE         0x50001000ul
#define SF32LB52_PINMUX1_BASE        0x50003000ul
#define SF32LB52_ATIM1_BASE          0x50004000ul
#define SF32LB52_EPIC_BASE           0x50007000ul
#define SF32LB52_LCDC1_BASE          0x50008000ul
#define SF32LB52_I2S1_BASE           0x50009000ul
#define SF32LB52_HPSYS_CFG_BASE      0x5000b000ul
#define SF32LB52_AES_BASE            0x5000d000ul
#define SF32LB52_TRNG_BASE           0x5000f000ul
#define SF32LB52_MPI1_BASE           0x50041000ul
#define SF32LB52_MPI2_BASE           0x50042000ul
#define SF32LB52_SDMMC1_BASE         0x50045000ul
#define SF32LB52_PTC1_BASE           0x50080000ul
#define SF32LB52_DMAC1_BASE          0x50081000ul
#define SF32LB52_MAILBOX1_BASE       0x50082000ul
#define SF32LB52_USART1_BASE         0x50084000ul
#define SF32LB52_USART2_BASE         0x50085000ul
#define SF32LB52_USART3_BASE         0x50086000ul
#define SF32LB52_GPADC_BASE          0x50087000ul
#define SF32LB52_AUDCODEC_BASE       0x50088000ul
#define SF32LB52_GPTIM1_BASE         0x50090000ul
#define SF32LB52_BTIM1_BASE          0x50092000ul
#define SF32LB52_WDT1_BASE           0x50094000ul
#define SF32LB52_SPI1_BASE           0x50095000ul
#define SF32LB52_SPI2_BASE           0x50096000ul
#define SF32LB52_I2C1_BASE           0x5009c000ul
#define SF32LB52_I2C2_BASE           0x5009d000ul
#define SF32LB52_GPIO1_BASE          0x500a0000ul
#define SF32LB52_GPTIM2_BASE         0x500b0000ul
#define SF32LB52_BTIM2_BASE          0x500b1000ul
#define SF32LB52_HPSYS_AON_BASE      0x500c0000ul
#define SF32LB52_LPTIM1_BASE         0x500c1000ul
#define SF32LB52_PMUC_BASE           0x500ca000ul
#define SF32LB52_RTC_BASE            0x500cb000ul
#define SF32LB52_IWDT_BASE           0x500cc000ul

/* ---- LPSYS 外设 ---- */

#define SF32LB52_LPSYS_RCC_BASE      0x40000000ul
#define SF32LB52_DMAC2_BASE          0x40001000ul
#define SF32LB52_MAILBOX2_BASE       0x40002000ul
#define SF32LB52_PINMUX2_BASE        0x40003000ul
#define SF32LB52_USART4_BASE         0x40005000ul
#define SF32LB52_USART5_BASE         0x40006000ul
#define SF32LB52_BTIM3_BASE          0x40009000ul
#define SF32LB52_LPSYS_CFG_BASE      0x4000f000ul
#define SF32LB52_LPSYS_AON_BASE      0x40040000ul
#define SF32LB52_GPIO2_BASE          0x40080000ul

#endif /* __ARCH_ARM_SRC_SF32LB52_SF32LB52_MEMORYMAP_H */