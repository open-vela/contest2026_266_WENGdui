############################################################################
# apps/thermo_panel/Makefile
# Thermo Panel Application for OpenVela (NuttX)
#
# 本目录承载两类 NSH 内置命令:
#   * thermopanel : 温控面板主应用 (LVGL UI), 由 CONFIG_THERMO_PANEL 控制
#   * rx (别名 rxd): RX 字节级诊断打印命令, 由 CONFIG_THERMO_RXDIAG 控制
# 两者相互独立: 只开 rx 时不会去编译依赖 LVGL 的 thermo_*.c。
############################################################################

include $(APPDIR)/Make.defs

# ---- 温控面板主应用 (依赖 LVGL, 见 Kconfig 的 THERMO_PANEL) ----
# 【改为照厂家移植】:
#   thermo_ui.c       = 厂家 app/src/thermo_ui.c 原文 (控件/布局/事件回调一字未改)
#   thermo_ui_glue.c  = 适配胶水: 厂家依赖的 font_medium/xiaozhi_font/参数全局,
#                       以及 LVGL 显示落屏 (对应厂家 lv_lcd.c 的 flush 通路)
#   原先本目录那套"自写简化版 UI"(thermo_app.c/thermo_temp.c/... 那批源文件)
#   已不再参与编译 —— 它们既非厂家原文, 也从未真正编进过固件
#   (旧 Kconfig 依赖不存在的 LIB_LVGL, 该项永远选不上)。
ifeq ($(CONFIG_THERMO_PANEL),y)
# thermo_font.c = 厂家 DroidSansFallback.ttf 的界面字符子集 (pyftsubset 裁剪),
#                 照厂家做法转成 C 数组 xiaozhi_font[], 由胶水层用
#                 lv_tiny_ttf_create_data() 在运行期建字体 (见 thermo_font.c 头注释)。
CSRCS += thermo_ui.c thermo_ui_glue.c thermo_font.c
endif

# ---- rx: RX 诊断 NSH 内置命令 (无 LVGL 依赖, 极轻量) ----
# 【M5: 命令名 rxd -> rx】
#   rxd 有 3 个字符, 而 RX 通道在 115200 下会丢"当前这一批的最后 1~2 个
#   字节" —— 敲 `rxd` 回车时真正进到 NSH 的常常只有 "rx", 于是这条命令
#   自己反而跑不起来。改用最短的 2 字符名 `rx`, 即使再丢尾巴, "rx" 也已
#   足够命中, 这条诊断命令才真正可用。
#   同时保留原 `rxd` 作为别名(第二个 MAINSRC/PROGNAME 对), 老习惯不变。
#
# 参照 NuttX 内置命令规范: MAINSRC / PROGNAME 一一对应(按出现顺序配对),
# Application.mk 会以 -Dmain=<PROGNAME>_main 重命名入口并写入内置注册表。
# 所以两个 PROGNAME 必须各配一个独立的 MAINSRC(同一个 .c 不能登记两次,
# 否则对象名冲突, -Dmain 会互相覆盖)。

ifeq ($(CONFIG_THERMO_RXDIAG),y)
MAINSRC   += rxd_main.c rxd_alias.c
PROGNAME  += rx rxd
PRIORITY  += 100 100
STACKSIZE += 2048 2048
endif

# MODULE 用于区分内置(y) / 可加载模块(m); 这里都按内置处理
MODULE = $(CONFIG_THERMO_PANEL)$(CONFIG_THERMO_RXDIAG)

include $(APPDIR)/Application.mk