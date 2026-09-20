# SF32LB52 智能温控面板（openvela / NuttX 移植 + thermopanel 应用）

> **2026 首届 openvela AI 硬件开发者大赛 · 新硬件平台适配赛道**
> 目标硬件：**思澈科技 SF32LB52-LCD**（Cortex-M33 双核 / 片内 512KB SRAM / 片外 16MB NOR + 8MB PSRAM / CO5300 LCD / FT6146 触摸）

## 作品简介

本项目完成 **openvela（NuttX）在 SF32LB52 平台上从零的芯片级移植**，并在其上跑通一套完整的**智能温控面板**应用：

- **系统移植**：芯片层（Cortex-M33 启动、时钟、USART 控制台、GPIO、GPADC、GPTIM PWM、LCDC 显示、LCDC1/GPIO1 中断）+ 板级层 + `sf32lb52-lcd` 板卡 defconfig，全部以**新增代码**方式进入 openvela 源码树，不依赖思澈 RT-Thread 侧的任何编译产物即可独立构建。
- **LCD 显示**：CO5300（QSPI，`spi_clk=48MHz`），LVGL 9.2.1 整屏 RGB565 缓冲放 PSRAM（`0x60200000`，FULL 渲染模式），一次整屏 390×450 刷新。
- **触摸**：FT6146（I2C `0x38`，PA30/PA33/PA31 中断/PA09 复位），LVGL 中断驱动 indev。
- **网热点**：NTC 腔体温采集（PA28/GPDAC ch0），增量式 PID 控温（`kp=6.0 ki=0.2 kd=1.0`），风扇 PWM（PA32/GPTIM2_CH1），加热灯（PA25）与到温灯（PA24）双灯翻转，NVS 参数持久化（目标温度 55.0°C）。
- **UI**：iOS 深色风格 LVGL 界面（主页：状态胶囊 / 大字温度 / 环境 / 目标 / PID 柱状图 / −·电源·+ 三键；设置页：目标温度与超温上限滑条；告警遮罩）。字体用 DroidSansFallback 子集 TTF 经 `lv_tiny_ttf` 生成。

## 所属赛道

**新硬件平台适配（重点鼓励 + 技术难度加分）**：openvela/NuttX 此前无 SF32LB52 官方支持，本作品完成全新芯片平台首刷，并叠加温控产品 Demo（采集 / 显示 / 控温 / 双灯联动），实现“系统适配 + 应用 Demo”全栈落地。

## 目录结构

```
openvela_thermo/
├── apps/thermo_panel/               # thermopanel 应用（NSH 内置命令）
│   ├── thermo_ui.c                  # 深色 iOS 风格 LVGL UI
│   ├── thermo_ui_glue.c             # 适配胶水层（LCD/触摸/字体/事件/控温循环）
│   ├── thermo_ui.h · thermo_app.h   # 应用接口
│   ├── thermo_font.c                # DroidSansFallback 界面字符子集 (C 数组)
│   ├── rtthread.h                   # rt_kprintf/rt_snprintf -> printf/snprintf shim
│   ├── rxd_main.c / rxd_alias.c     # RX 字节诊断命令 (rx/rxd)
│   └── Makefile · Kconfig
├── boards/arm/sf32lb52/sf32lb52-lcd/ # 板级层（含厂家 LCD/触摸/PID/NVS 静态库集成）
├── arch(arm/src/sf32lb52)/           # 芯片层（Cortex-M33 启动/时钟/串口/GPIO/ADC/PWM）
├── drivers/                           # 底层驱动补充
├── sdk_port/                          # 厂家 HAL 移植胶水（co5300/ft6146/pid/nvs/fan…）
│   └── build_vendor_lcd_lib.sh        # 生成 libsf32lb52_vendorlcd.a
├── integrate.sh                       # 一键集成进 openvela 源码树（复制+注册+编库）
├── build_openvela.sh                  # 一键构建（distclean→integrate→configure→make→objcopy）
├── SF32LB52_NuttX_Porting_Guide.md    # 详细移植指南
└── README.md                          # 本文件
```

## 运行方式（构建 + 烧录）

环境：WSL Ubuntu-24.04 + openvela 源码树（`nuttx/`、`apps/` 的父目录），原生 `arm-none-eabi-gcc 13.2.1`。

```bash
cd openvela_thermo
export OPENVELA_ROOT=/path/to/openvela_parent      # 含 nuttx/ 与 apps/
bash build_openvela.sh                              # distclean → integrate → configure(sf32lb52-lcd:thermo) → make → nuttx.bin
```

- 关键校验：`CONFIG_ARCH_CHIP_SF32LB52=y`、`CONFIG_ARCH_FAMILY="armv8-m"`、`CONFIG_INIT_ENTRYPOINT="nsh_main"`。
- 产物 `nuttx.bin` 必须 < 0x240000（2.25MB，ER_IROM1 分区）。

烧录（沿用思澈 sftool，整包含 ftab/bootloader/ER_IROM1/2/3）：

```bash
cp openvela_thermo/nuttx.bin devkit_lcd_n16r8_firmware/main.bin/ER_IROM1.bin
# 用 devkit_lcd_n16r8_firmware/sftool_param.json 整包烧写
sftool -c SF32LB52 -m nor write_flash -i sftool_param.json
```

串口 115200 预期日志：

```
[BOARD] SF32LB52-LCD bringup
[LCD] drawbuf @ 0x60200000 (351000B) psram=1 mode=FULL
[THERMO] UI init done
[CTRL] meas=.. out=..% heater=ON setpoint_led=off (PA25/PA24)
NuttShell (NSH)
nsh>
```

## 未完成 / 已知限制（如实声明）

以下功能点未能完整落地或未做整机验证，供评委如实参考：

| 功能 | 状态 | 说明 |
|------|------|------|
| **蓝牙 BLE / 无线** | ❌ 未实现 | SF32LB52 为蓝牙 SoC，但 openvela/NuttX 下未移植 BLE 协议栈；当前完全为有线控制（按键/触摸），无无线链路。 |
| **低功耗 / 休眠** | ⚠️ 浅层实现 | `sf32lb52_pm.c` 提供 `enter_stop()`（背光关闭 + WFI 进 STOP + 触摸唤醒），但未做整机功耗实测与休眠-唤醒真机回归，寄存器定义仍需真机核对。 |
| **双核 LCPU** | ❌ 未实现 | 双核中的 LCPU 未启动驱动；NuttX 仅运行在 HCPU（M33），LCPU 的 mailbox 唤醒/协作未实现。 |
| **串口 RX（指令输入）** | ⚠️ 待验证 | 串口 TX/控制台日志可稳定输出；RX 读入链路经过多轮调优（`fix_rx*.sh`），命令回读路径稳定性未做压力实测。 |
| **温控精度 / 传感器** | ⚠️ 有实现未标定 | NTC 腔体采集 + 增量式 PID 有完整实现；但板载无环境传感器，环境温度用哨兵 `THERMO_AMBIENT_NA(-999)` 显示" --"。整机控温曲线、稳态误差、PID 参数未做长时间实测标定。 |
| **故障 / 过温报警** | ⚠️ 逻辑有、注入未验证 | `thermo_fault.c` 已实现超温锁存、传感器断线、ADC 错误检测并联动 UI 告警遮罩；但故障场景未做真机注入验证（未实测超温实际触发告警）。 |

如需复现，直接使用第二小节"已知限制"所列的哨兵值与调试口即可观察到对应行为。

## 比赛提交约定（按官方《参赛代码提交指南》）

- 代码存放于组委会专属仓 `contest2026_<编号>_<队伍名>` 的子目录，与 openvela 工程通过 manifest `<linkfile>` 映射。
- fork 专属仓 → 开发 → PR → 自行 review 合入；AI 对话日志导出至仓内 `logs/`。
- 提交截止 **9 月 20 日**；首次 PR 需签署 openvela CLA。