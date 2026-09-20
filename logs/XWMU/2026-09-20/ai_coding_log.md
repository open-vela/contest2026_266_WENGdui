# AI 辅助开发日志 — SF32LB52-LCD 温控面板移植

- **队伍**：contest2026_266_WENGdui
- **开发者 GitHub**：XWMU
- **时间**：2026-09
- **类型**：新硬件平台适配（openvela / NuttX）
- 说明：本文件为 AI 协作过程的人可读摘要。官方要求的 JSONL 格式日志需用赛委会提供的「AI Coding 日志归集工具」从 AI 工具本机 staging 导出，另置于本目录（logs/XWMU/<date>/<tool>__<sid>.jsonl）。

## 一、项目目标

将 openvela（NuttX）从零移植到思澈科技 SF32LB52-LCD（Cortex-M33 双核 / 512KB SRAM / 16MB NOR + 8MB PSRAM / CO5300 LCD / FT6146 触摸），并跑通完整的智能温控面板应用（NTC 采集 → PID 控温 → 风扇/加热灯/到温灯联动 → LVGL 深色 UI）。

## 二、AI 在各环节的参与

| 环节 | AI 协助内容 | 实际帮助 |
|------|-------------|----------|
| 需求拆解 | 从赛程与赛道指南拆出「系统移植 + 温控 Demo」全栈目标，列出 M0~M7 里程碑 | 明确优先级，避免返工 |
| 方案设计 | 分析厂家 SDK，确定芯片层/板级层/应用层三层结构，选择 LVGL 9.2.1 + 帧缓冲放 PSRAM | 结构清晰可独立构建 |
| 编码 | 生成官网文档、Makefile、Kconfig、链接脚本；撰写 NTC 采集、增量式 PID、PWM 风扇、双灯联动、LVGL 深色 UI | 大幅提升产出速度 |
| 调试 | 定位「WSL 路径中文 + gcc.exe 无法读取」的构建失败；无浮点库下 `%.1f` 损坏改用 `fmt_1dp()`；readpixel 高端错位问题降级 | 逐个解决真机/构建瓶颈 |
| 文档 | 编写根 README 与移植指南，并如实标注未完成/已知限制 | 准确呈现作品边界 |
| 提交 | 按官方《参赛代码提交指南》整理目录、更新 manifest linkfile、签名修复合入 PR | 顺利通过 CLA 合并 |

## 三、关键决策与教训

1. **工具链**：必须用 Linux 原生 `arm-none-eabi-gcc 13.2.1`；指向 Windows .exe 的包装脚本读不了 `/mnt/d` 中文路径会 `Permission denied`。
2. **浮点显示**：板级 NuttX 未开 `CONFIG_LIBC_FLOATINGPOINT`，`%.1f` 输出损坏，需用整数拼装一位小数（`fmt_1dp()`）。
3. **烧录地址**：用户代码分区为 `ER_IROM1@0x12218000`，用 devkit 的 `sftool_param.json` 整包含 bootloader/ftab 一次烧写（非旧指南 `0x10040000`）。
4. **提交流程**：open-vela 组织仓库禁止直接 push，必须 fork → PR → 自行合入；提交邮箱需用公开邮箱并先签 CLA，否则 `cla/signature` 不通过。

## 四、未完成 / 已知限制（如实声明）

详见作品根 `README.md`：蓝牙 BLE、双核 LCPU 未实现；低功耗/休眠为浅层实现未整机实测；串口 RX 待压力验证；温控无环境传感器、PID 未长时标定；故障报警有逻辑但未真机注入验证。

## 五、产物

- `board/contest_board/`：SF32LB52 芯片+板级移植 + thermopanel 应用 + 厂家 HAL 胶水
- `README.md`：作品说明（含已知限制）
- `logs/XWMU/`：本 AI 协作日志目录