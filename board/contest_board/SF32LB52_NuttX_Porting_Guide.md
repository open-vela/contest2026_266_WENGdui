# SF32LB52X → NuttX / openvela 芯片移植指南（已按真实硬件重写）

> ⚠️ 本文件已重写。旧版本内容基于 **STM32F1**（`RCC->CR` / `GPIOA->CRH` / `USART1->BRR` / `TIM2->CCR1`）
> 与 **Cortex-M0+ 64K Flash / 16K SRAM** 的错误描述，与 SF32LB52X 完全不符，已作废。
> 下方所有地址、中断号、内存布局均取自工程内真实 SDK：
> `源码/xiaozhi-sf32-1.4.0/sdk/drivers/cmsis/sf32lb52x/`

## 1. 芯片真实规格（以 SDK 为准）

| 项目 | 真实值 | 依据 |
|------|--------|------|
| 内核 | **Cortex-M33** (Armv8-M)，含 FPU / DSP / MPU / VTOR | `register.h`: `__CM33_REV`、`__MPU_PRESENT 1`、`__FPU_PRESENT 1` |
| NVIC 优先级位 | **3 位** | `register.h`: `__NVIC_PRIO_BITS 3` |
| 双核 | **HCPU（高性能 M33）+ LCPU**，各自独立地址空间 | `register.h`: `SOC_BF0_HCPU` / `SOC_BF0_LCPU` |
| 片内 SRAM | **512 KB** | `mem_map.h`: RAM0 128K + RAM1 128K + RAM2 256K |
| 片外 NOR | **16 MB**（N16） | 板名 `sf32lb52-lcd_n16r8` |
| 片外 PSRAM | **8 MB**（R8），映射 0x60000000 | `mem_map.h`: `PSRAM_BASE 0x60000000` |
| RTOS 现状 | 现工程为 **RT-Thread + SiFli SDK** | `link.lds` 引用 `rtconfig.h` / `FSymTab` / `finsh` |

> 注意：openvela / NuttX 目前**没有** SF32LB52 官方支持，思澈生态为 RT-Thread。
> 因此这是一次**从零的芯片级移植**，不是改配置。工作量以月计。

## 2. 内存映射（真实地址）

```c
/* ---- SRAM (mem_map.h) ---- */
#define HPSYS_RAM0_BASE     0x20000000UL   /* 128KB, DTCM + retention */
#define HPSYS_RAM1_BASE     0x20020000UL   /* 128KB */
#define HPSYS_RAM2_BASE     0x20040000UL   /* 256KB */
#define HPSYS_RAM_SIZE      (512*1024)     /* 合计 512KB */

/* ---- NOR flash 映射 (QSPI1 / MPI1) ---- */
#define QSPI1_MEM_BASE      0x10000000UL   /* NOR XIP 映射 */
#define QSPI2_MEM_BASE      0x12000000UL
#define PSRAM_BASE          0x60000000UL   /* 8MB OPI-PSRAM */
```

### NOR 启动布局（真实，勿改）

```
0x10000000  flash table      (20KB)
0x10005000  cal table        (8KB)
0x10010000  boot patch       (64KB)
0x10020000  bootloader       (128KB)
0x10040000  user code        ← 应用程序入口
```

## 3. 外设基地址（真实）

| 外设 | 基地址 | 实例宏 |
|------|--------|--------|
| HPSYS RCC 时钟 | `0x50000000` | `hwp_hpsys_rcc` |
| LPSYS RCC 时钟 | `0x40000000` | `hwp_lpsys_rcc` |
| **GPIO1** | `0x500A0000` | `hwp_gpio1` |
| **GPIO2** | `0x40080000` | `hwp_gpio2` |
| **USART1** | `0x50084000` | `hwp_usart1` |
| USART2 | `0x50085000` | `hwp_usart2` |
| USART3 | `0x50086000` | `hwp_usart3` |
| USART4 (LPSYS) | `0x40005000` | `hwp_usart4` |
| **GPADC** | `0x50087000` | `hwp_gpadc` |
| **SPI1** | `0x50095000` | `hwp_spi1` |
| SPI2 | `0x50096000` | `hwp_spi2` |
| **GPTIM1** | `0x50090000` | `hwp_gptim1` |
| **GPTIM2** | `0x500B0000` | `hwp_gptim2` |
| LCDC1 (显示) | `0x50008000` | `hwp_lcdc1` |
| MPI1 (QSPI1/NOR) | `0x50041000` | `hwp_mpi1` |
| MPI2 (QSPI2) | `0x50042000` | `hwp_mpi2` |
| PMUC 电源管理 | `0x500CA000` | `hwp_pmuc` |
| RTC | `0x500CB000` | `hwp_rtc` |
| HPSYS AON | `0x500C0000` | `hwp_hpsys_aon` |
| PINMUX1 | `0x50003000` | `hwp_pinmux1` |

## 4. 中断号（真实，取自 `IRQn_Type`，HCPU）

| 中断 | 编号 | 中断 | 编号 |
|------|------|------|------|
| GPIO2 | 20 | RTC | 49 |
| PMUC | 48 | LCPU2HCPU | 58 |
| **USART1** | **59** | **SPI1** | **60** |
| I2C1 | 61 | LCDC1 | 63 |
| I2S1 | 64 | **GPADC** | **65** |
| AES | 67 | PTC1 | 68 |
| TRNG | 69 | **GPTIM1** | **70** |
| **GPTIM2** | **71** | BTIM1/2 | 72/73 |
| USART2 | 74 | SPI2 | 75 |
| I2C2 | 76 | SDMMC1 | 79 |
| **GPIO1** | **84** | MPI1 | 85 |
| MPI2 | 86 | USART3 | 95 |

## 5. 移植分层与文件清单

```
nuttx/arch/arm/src/sf32lb52/          ← 芯片层（需新建）
    chip.h                            ← 芯片总头（已完成）
    sf32lb52_memorymap.h              ← 内存/外设地址（已完成）
    sf32lb52_irq.h                    ← 中断号（已完成）
    sf32lb52_start.c                  ← 复位入口、时钟初始化、段拷贝
    sf32lb52_clockconfig.c            ← PLL/时钟树（依据 hpsys_rcc.h）
    sf32lb52_lowputc.c                ← 控制台 USART1 轮询收发
    sf32lb52_serial.c                 ← USART 中断驱动
    sf32lb52_timerisr.c               ← SysTick 1ms 时基
    sf32lb52_gpio.c                   ← GPIO 驱动（依据 gpio1.h/gpio2.h）
    sf32lb52_gpadc.c                  ← NTC 采集（2 路）
    sf32lb52_qspi.c / sf32lb52_lcd.c  ← 屏幕（QSPI/LCDC，非 SPI ST7789）
    sf32lb52_pwm.c                    ← 风机调速（GPTIM）
    sf32lb52_pm.c                     ← 低功耗
    Kconfig / Make.defs

nuttx/boards/arm/sf32lb52/sf32lb52-lcd/   ← 板级层（需新建）
    include/board.h
    scripts/flash.ld / ram.ld
    src/sf32lb52_bringup.c / sf32lb52_appinit.c
    configs/nsh/defconfig
```

## 6. 关键实现约束（不改就是坑）

1. **必须复用厂家 bootloader**。NOR 的 `0x10020000` 处是思澈 bootloader，负责 QSPI 初始化、
   PSRAM 初始化与代码搬运。NuttX 镜像应放在 `0x10040000`，**不要**自己重写启动搬运逻辑。
2. **XIP 运行**。代码从 NOR 直接执行，链接地址用 `0x10000000` 段，链接脚本须参考
   `Templates/gcc/hcpu/link.lds`（含 `.retm_data` 等必须在 RAM 执行的段）。
3. **双核**。LCPU 由 HCPU 通过 mailbox（`MAILBOX1/2`）启动，HCPU 侧 NuttX 若不管 LCPU，
   需在 defconfig 中禁用相关中断（`LCPU2HCPU_IRQn`）避免误触发。
4. **工具链**：Cortex-M33，需 `-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard`（与 SDK 一致核实）。
5. **显示不是 SPI ST7789**：本板为 QSPI 面板（22p QSPI FPC），驱动走 MPI/LCDC，旧指南错误。

## 7. 分阶段里程碑（每阶段必须真机验证后再进下一阶段）

| 阶段 | 目标 | 验收标准 |
|------|------|----------|
| M0 | openvela 环境打通 | 在 Ubuntu 成功编译 `simulator` 或 `mps3-an547`，跑起来 |
| M1 | 芯片能启动 | NuttX 链接到 `0x10040000`，烧录后不死机（用 JLink 挂上看 PC） |
| M2 | **串口控制台** | `nsh>` 提示符出现在 UART1（PA19/PA18 调试口） |
| M3 | 时钟/时基 | `sleep 1` 实测误差 < 5% |
| M4 | GPIO | 能控制背光/继电器/LED |
| M5 | GPADC | NTC 短接 GND 读数跳变 |
| M6 | 显示 | QSPI 面板点亮 LVGL |
| M7 | PWM + PM | 风机调速、休眠唤醒 |

## 7b. 未完成 / 已知限制（如实声明）

> 本作品为竞赛提交，如实标注哪些能力**尚未实现**、哪些**有实现但未做整机真机验证**，避免给评委造成"已完成"的误判。

| 能力 | 状态 | 说明 |
|------|------|------|
| **蓝牙 BLE / 无线** | 未实现 | 板为蓝牙 SoC，但 openvela/NuttX 未移植 BLE 栈；当前纯有线（按键/触摸），无无线链路。 |
| **低功耗 / 休眠** | 浅层实现 | `sf32lb52_pm.c` 的 `enter_stop()`（关背光 + WFI + 触摸唤醒）可用，但未整机功耗实测、未做休眠-唤醒真机回归，PMUC 寄存器定义需真机核对。 |
| **双核 LCPU** | 未实现 | 仅运行 HCPU；LCPU 启动与 mailbox 协作未驱动。 |
| **串口 RX（指令输入）** | 待验证 | TX/日志稳定；RX 读入历经多轮调优，命令回读稳定性未压力实测。 |
| **温控精度 / 传感器** | 有实现未标定 | NTC+PID 完整；无环境传感器，环境温度用哨兵 `-999`；PID 参数、稳态误差未做长时实测标定。 |
| **故障 / 过温报警** | 逻辑有、注入未验证 | `thermo_fault.c` 超温锁存/断线/ADC 错误检测实现齐全并联动 UI 告警；未真机注入故障验证触发路径。 |

里程碑 M1/M3/M6 的地址曾以旧版（`0x10040000`、`nsh`、`/mnt/hgfs`）撰写，已按真实流程在下一节修订；PM 相关的 M7 里程碑整体视为"待真机验证"。`sf32lb52_serial.c` / `sf32lb52_pm.c` 头部注释中残留的 "Cortex-M0+" 为早期复制错误，本作实际为 **Cortex-M33**，请以本文档为准。

## 8. 构建与集成（真实流程，已验证）

本项目已提供 `integrate.sh` 与 `build_openvela.sh` 一键脚本，对应本章节全部步骤。

```bash
# 本仓库目录（注意：路径含中文，编译需在 Linux 原生盘进行）
cd openvela_thermo

# 把芯片层 / 板级层 / thermopanel 应用集成进 openvela 源码树
export OPENVELA_ROOT=/path/to/openvela_parent     # 含 nuttx/ 与 apps/ 的父目录
bash integrate.sh                                  # 复制 + 注册 + 编译厂家 vendor 静态库

# 一键构建
bash build_openvela.sh                             # distclean → integrate → configure → make → objcopy
```

`build_openvela.sh` 内部等价于：

```bash
distclean
bash integrate.sh                                  # 生成 configure 配置
./tools/configure.sh sf32lb52-lcd:thermo           # 板卡 config：sf32lb52-lcd 板、thermo 应用
make -j$(nproc)
arm-none-eabi-objcopy -O binary nuttx nuttx.bin    # 生成烧录镜像
size nuttx.bin                                     # 校验 < 0x240000 (ER_IROM1 分区上限)
```

> ⚠️ 工具链必须用 **Linux 原生 `arm-none-eabi-gcc 13.2.1`**（`/usr/bin`）。
> 若 PATH 里是包装脚本指向 Windows `gcc.exe`，读不了 `/mnt/d` 中文路径会 `Permission denied`：
> ```bash
> export PATH=/usr/bin:/bin:/usr/sbin:$PATH
> ```

### 烧录（沿用思澈 sftool，整包烧写）

用户代码所在分区的地址是 **ER_IROM1@0x12218000**（旧指南 0x10040000 是 NOR XIP 映射，非烧录地址）。用 devkit 的 `sftool_param.json` 整包含 `bootloader/ER_IROM1/ER_IROM2/ER_IROM3/ftab` 一次烧写：

```bash
cp nuttx.bin <devkit>/main.bin/ER_IROM1.bin        # 覆盖用户代码分区
sftool -c SF32LB52 -m nor write_flash -i sftool_param.json
# 或用串口 tar 升级：sftool -p /dev/ttyUSB0 -c SF32LB52 -m nor write_flash ...
```

### thermopanel 应用说明

`sf32lb52-lcd:thermo` defconfig 内置 `thermo_panel` 应用：NTC 腔体采集 → PID 控温 → 风扇/加热灯/到温灯联动 → LVGL 深色 UI（主页+设置页+告警），NVS 持久化目标温度。首个用户任务即 `nsh_main`，按下电源键进入控温主界面。