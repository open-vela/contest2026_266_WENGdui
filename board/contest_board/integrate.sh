#!/bin/bash
# ============================================================
# SF32LB52X 温控面板 — openvela 集成脚本
#
# 将芯片层 / 板级层 / 应用层文件复制进 openvela 源码树,
# 并完成 Kconfig 注册 (芯片选择 / 板级选择 / 应用注册)。
#
# 用法:
#   export OPENVELA_ROOT=~/vela-opensource
#   bash integrate.sh
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -z "$OPENVELA_ROOT" ]; then
    echo "ERROR: 未设置 OPENVELA_ROOT"
    echo "  export OPENVELA_ROOT=~/vela-opensource"
    exit 1
fi

NUTTX_DIR="$OPENVELA_ROOT/nuttx"
APPS_DIR="$OPENVELA_ROOT/apps"

if [ ! -d "$NUTTX_DIR/arch/arm" ]; then
    echo "ERROR: $NUTTX_DIR 不是有效的 nuttx 源码树"
    exit 1
fi
if [ ! -d "$APPS_DIR" ]; then
    echo "ERROR: $APPS_DIR 不存在"
    exit 1
fi

echo "============================================"
echo "  SF32LB52X openvela 集成"
echo "  NUTTX_DIR = $NUTTX_DIR"
echo "  APPS_DIR  = $APPS_DIR"
echo "============================================"

# ------------------------------------------------------------
# 1. 芯片层 arch
# ------------------------------------------------------------
echo "[1/6] 安装芯片层 arch/arm/src/sf32lb52 ..."
mkdir -p "$NUTTX_DIR/arch/arm/src/sf32lb52"
mkdir -p "$NUTTX_DIR/arch/arm/include/sf32lb52"

cp "$SCRIPT_DIR/arch/arm/src/sf32lb52/"*.h  "$NUTTX_DIR/arch/arm/src/sf32lb52/"
cp "$SCRIPT_DIR/drivers/"*.c               "$NUTTX_DIR/arch/arm/src/sf32lb52/"
cp "$SCRIPT_DIR/drivers/"*.h               "$NUTTX_DIR/arch/arm/src/sf32lb52/"

# 汇编源也必须复制!
#   sf32lb52_head.S 提供复位入口 __start —— 它要在任何 push 之前清
#   ARMv8-M 的 MSPLIM/PSPLIM(ROM/bootloader 设过, 用于保护自己的 64KB
#   RAM: BOOTLOADER_RAM_DATA_END_ADDR=0x20010000)。缺了它, 应用第一条
#   push 就触发栈越界异常, 现象是"串口只输出 SFBL"。
#
#   注意 1: 漏掉这一行的后果是 make 报 "没有规则可制作目标 sf32lb52_head.S"。
#   注意 2: 这里【必须】用 find -exec 而不是 `cp dir/*.S`, 因为
#           drivers/ 目录下没有 .S 文件时通配符不展开, cp 会直接报错;
#           本脚本带 set -e, 于是集成会在 [1/6] 处静默中断,
#           现象是"构建只打印几行就回到提示符"。

find "$SCRIPT_DIR/arch/arm/src/sf32lb52" -maxdepth 1 -name '*.S' \
     -exec cp {} "$NUTTX_DIR/arch/arm/src/sf32lb52/" \;
find "$SCRIPT_DIR/drivers" -maxdepth 1 -name '*.S' \
     -exec cp {} "$NUTTX_DIR/arch/arm/src/sf32lb52/" \;
cp "$SCRIPT_DIR/arch/arm/src/sf32lb52/Make.defs" "$NUTTX_DIR/arch/arm/src/sf32lb52/"
# Kconfig 必须一并复制!
# 缺少它 -> ARCH_CHIP_SF32LB52 符号在 Kconfig 体系中不存在
#        -> defconfig 里的 CONFIG_ARCH_CHIP_SF32LB52=y 被当作未知符号丢弃
#        -> select ARCH_CORTEXM33 不执行 -> -mcpu 落到默认值
#        -> 汇编报 "selected processor does not support `mrs r5,basepri'"
cp "$SCRIPT_DIR/arch/arm/src/sf32lb52/Kconfig"  "$NUTTX_DIR/arch/arm/src/sf32lb52/"

# 芯片公共 arch 头 (NR_IRQS 等), configure 时会链接为 include/arch/chip
cp "$SCRIPT_DIR/arch/arm/include/sf32lb52/"*.h "$NUTTX_DIR/arch/arm/include/sf32lb52/"

# ------------------------------------------------------------
# 2. 板级层
# ------------------------------------------------------------
echo "[2/6] 安装板级层 boards/arm/sf32lb52 ..."
mkdir -p "$NUTTX_DIR/boards/arm/sf32lb52"
cp -r "$SCRIPT_DIR/boards/arm/sf32lb52/." "$NUTTX_DIR/boards/arm/sf32lb52/"

# ------------------------------------------------------------
# 2b. 厂家 LCD 静态库 (libsf32lb52_vendorlcd.a)
#
#   库内容: 厂家 HAL (lcdc/rcc/gpio/pinmux/tim/pin_const) + 厂家屏驱动
#           co5300.c + 胶水层, 用一个"优先于 NuttX"的头文件环境独立编译。
#
#   为什么必须在这里构建: 该库会被板级 scripts/Make.defs 通过 LDLIBS
#   链接, 而它在 CONFIG_SF32LB52_HAS_LCDC=y 时是硬性依赖 ——
#   不产出会直接 $(error) 报错。
#
#   目的目录是刚复制好的板级 src/, 所以必须在 [2/6] 之后执行。
# ------------------------------------------------------------
echo "[2b] 构建厂家 LCD 静态库 ..."
bash "$SCRIPT_DIR/build_vendor_lcd_lib.sh"

# ------------------------------------------------------------
# 3. 应用层
# ------------------------------------------------------------
echo "[3/6] 安装应用层 apps/thermo_panel ..."
mkdir -p "$APPS_DIR/thermo_panel"
cp "$SCRIPT_DIR/apps/thermo_panel/"* "$APPS_DIR/thermo_panel/"

if [ -f "$APPS_DIR/Kconfig" ] && ! grep -q "thermo_panel" "$APPS_DIR/Kconfig"; then
    echo 'source "$APPSDIR/thermo_panel/Kconfig"' >> "$APPS_DIR/Kconfig"
    echo "  已注册 thermo_panel 到 apps/Kconfig"
fi

# ------------------------------------------------------------
# 4. 注册芯片 Kconfig 与 ARCH_CHIP / ARCH_BOARD 字符串
#
# 实测事实 (openvela, 本源码树):
#   * 【不存在】arch/arm/src/Kconfig 与 boards/arm/Kconfig 这两个文件!
#     芯片/板级 Kconfig 必须分别挂到 arch/arm/Kconfig 与 boards/Kconfig。
#     之前写向那两个不存在的文件, 因 `[ -f ]` 判断失败而静默跳过,
#     导致符号从未注册、且没有任何报错 (最难查的一类问题)。
#   * config ARCH_CHIP / config ARCH_BOARD 都是 string, 通过
#     default "目录名" if ARCH_CHIP_XXX / ARCH_BOARD_YYY 映射目录,
#     缺失会导致 Make.defs 与板级目录找不到。
# ------------------------------------------------------------
echo "[4/6] 注册芯片与板级 Kconfig ..."

inject_source() {
    local f="$1"
    local line="$2"
    if [ ! -f "$f" ]; then
        echo "  [FAIL] 找不到 $f"
        exit 1
    fi
    if grep -qF "$line" "$f"; then
        return 0
    fi
    printf '\n%s\n' "$line" >> "$f"
    echo "  已注入: $line  ->  $f"
}

# 4a. 芯片附加选项 Kconfig -> arch/arm/Kconfig
#     无条件 source 即可: arch/arm/src/sf32lb52/Kconfig 内部
#     已用 `if ARCH_CHIP_SF32LB52` 保护附加选项 (芯片符号本身不放在那里)。
inject_source "$NUTTX_DIR/arch/arm/Kconfig" \
              'source "arch/arm/src/sf32lb52/Kconfig"'

# 4a2. 【关键】芯片符号必须注入到 arch/arm/Kconfig 的"MCU choice"内部!
#
#   arch/arm/Kconfig:55   choice
#   arch/arm/Kconfig:57     default ARCH_CHIP_STM32
#   arch/arm/Kconfig:714  endchoice # ARM MCU selection
#
#   芯片符号若定义在 choice 之外, choice 内无成员被选中 -> 落回
#   ARCH_CHIP_STM32=y, 连带 ARCH_CHIP_STM32F103ZE (Cortex-M3),
#   于是 select ARCH_CORTEXM3 -> select ARCH_ARMV7M ->
#   CONFIG_ARCH_FAMILY="armv7-m" (而非 armv8-m) ->
#   ARCH_SUBDIR=armv7-m -> VPATH 缺 armv8-m ->
#       make: 没有规则可制作目标 "arm_sau.c"
#   且 arm_m/arm_vectors.c 走 #elif ARMV7M 分支 ->
#       error: 'ARMV7M_PERIPHERAL_INTERRUPTS' undeclared
#
#   注入点: `config ARCH_CHIP_NRF53` 之前 (必在 choice 内, 位置稳定)。
inject_chip_symbol() {
    local f="$NUTTX_DIR/arch/arm/Kconfig"
    if [ ! -f "$f" ]; then
        echo "  [FAIL] 找不到 $f"
        exit 1
    fi
    if grep -q '^config ARCH_CHIP_SF32LB52$' "$f"; then
        return 0
    fi
    if ! grep -q '^config ARCH_CHIP_NRF53$' "$f"; then
        echo "  [FAIL] $f 中找不到 config ARCH_CHIP_NRF53, 无法定位 choice 内部"
        exit 1
    fi

    awk '
      !done && $0 == "config ARCH_CHIP_NRF53" {
          print "config ARCH_CHIP_SF32LB52"
          print "\tbool \"SiFli SF32LB52X (Armv8-M Cortex-M33, 双核 HCPU+LCPU)\""
          print "\tselect ARCH_ARM_M"
          print "\tselect ARCH_ARMV8M"
          print "\tselect ARCH_CORTEXM33"
          print "\tselect ARCH_HAVE_MPU"
          print "\t---help---"
          print "\t\tSiFli SF32LB52X: 双核 (HCPU+LCPU) Armv8-M Cortex-M33,"
          print "\t\tHCPU 最高 240MHz, 片内 SRAM 512KB, 片外 NOR 16MB, PSRAM 8MB。"
          print "\t\t必须同时 select ARCH_ARM_M 与 ARCH_ARMV8M (openvela 双家族目录)。"
          print ""
          done = 1
      }
      { print }
    ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"

    echo "  已注入芯片符号 ARCH_CHIP_SF32LB52 -> $f (MCU choice 内部)"
}

inject_chip_symbol

# 4b. 板级 Kconfig -> boards/Kconfig
#     boards/arm/sf32lb52/Kconfig 内部同样已用 if ARCH_CHIP_SF32LB52 保护。
inject_source "$NUTTX_DIR/boards/Kconfig" \
              'source "boards/arm/sf32lb52/Kconfig"'

# 4b2. 【关键】板卡符号必须注入到 boards/Kconfig 的"板卡 choice"内部!
#
#   NuttX 的板卡选择是一个 choice (boards/Kconfig 第 6 行 -> 第 3242 行
#   endchoice), 默认项为 ARCH_BOARD_CUSTOM。
#   若板卡符号定义在 choice 之外, choice 内无成员被选中 -> 落回 CUSTOM=y,
#   于是 tools/Config.mk 走自定义分支:
#       CUSTOM_DIR 为空 -> BOARD_DIR = $(TOPDIR)/ 
#   表现为:
#       make: 没有规则可制作目标 ".../nuttx//scripts/flash.ld"
#       make: 没有规则可制作目标 "libboard.a"
#   注入点选在 `config ARCH_BOARD_CUSTOM` 之前 (必在 choice 内, 位置稳定)。
inject_board_symbol() {
    local f="$NUTTX_DIR/boards/Kconfig"
    if [ ! -f "$f" ]; then
        echo "  [FAIL] 找不到 $f"
        exit 1
    fi
    if grep -q '^config ARCH_BOARD_SF32LB52_LCD$' "$f"; then
        return 0
    fi
    if ! grep -q '^config ARCH_BOARD_CUSTOM$' "$f"; then
        echo "  [FAIL] $f 中找不到 config ARCH_BOARD_CUSTOM, 无法定位 choice 内部"
        exit 1
    fi

    awk '
      !done && $0 == "config ARCH_BOARD_CUSTOM" {
          print "config ARCH_BOARD_SF32LB52_LCD"
          print "\tbool \"SiFli SF32LB52-LCD (16MB NOR + 8MB PSRAM)\""
          print "\tdepends on ARCH_CHIP_SF32LB52"
          print "\t---help---"
          print "\t\tSF32LB52X 开发板, 板载 QSPI LCD 面板。"
          print "\t\tHCPU 240MHz, 片内 SRAM 512KB, 片外 NOR 16MB, 片外 PSRAM 8MB。"
          print ""
          done = 1
      }
      { print }
    ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"

    echo "  已注入板卡符号 ARCH_BOARD_SF32LB52_LCD -> $f (choice 内部)"
}

inject_board_symbol

# 4c. 字符串默认值注入 (ARCH_CHIP / ARCH_BOARD)
inject_string_default() {
    local f="$1" cfgname="$2" defval="$3" cond="$4" desc="$5"
    [ -f "$f" ] || return 0
    if grep -q "default \"$defval\"" "$f"; then
        return 0
    fi
    if ! grep -q "^config ${cfgname}\$" "$f"; then
        echo "  [WARN] $f 中找不到 config ${cfgname}, 跳过 ${desc}"
        return 0
    fi

    awk -v cfg="$cfgname" -v dv="$defval" -v cd="$cond" '
      $0 == "config " cfg { print; in_cfg = 1; next }
      in_cfg && /^[[:space:]]*string/ {
          print
          printf "\tdefault \"%s\"\tif %s\n", dv, cd
          in_cfg = 0
          next
      }
      { print }
    ' "$f" > "$f.tmp" && mv "$f.tmp" "$f"

    echo "  已注入 ${desc}: default \"$defval\" if $cond  ->  $f"
}

inject_string_default "$NUTTX_DIR/arch/arm/Kconfig" ARCH_CHIP  sf32lb52      ARCH_CHIP_SF32LB52      "ARCH_CHIP"
inject_string_default "$NUTTX_DIR/boards/Kconfig"   ARCH_BOARD sf32lb52-lcd ARCH_BOARD_SF32LB52_LCD "ARCH_BOARD"

# ------------------------------------------------------------
# 4d. openvela Kconfig 语法归一化 —— 让 kconfig-frontends 也能解析
#
#   【为什么必须做】
#   本树 tools/Unix.mk:674 用
#       KCONFIG_LIB = $(shell command -v menuconfig 2> /dev/null)
#   来选 Kconfig 解析器:
#       PATH 里有 kconfiglib 提供的 menuconfig -> 用 kconfiglib (很宽容)
#       PATH 里没有                            -> 用 kconfig-conf (很严格)
#   为绕开 Windows 版 arm-none-eabi-gcc.exe 而把 PATH 收紧成
#       /usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
#   正好会把 ~/.local/bin/menuconfig 挤掉 -> KCONFIG_LIB 为空 ->
#   落到 kconfig-frontends 的 kconfig-conf。这一步就是给那种情形兜底。
#
#   kconfig-conf 对下面三类写法会直接报错, 让 olddefconfig 退出 1:
#
#   (1) `osource` (可选包含)
#       Linux 5.x 与 kconfiglib 才支持; kconfig-frontends 的 lexer 只认
#       `source` / `rsource`。本树命中:
#           apps/graphics/lvgl/Kconfig:28
#               osource "$APPSDIR/graphics/lvgl/lvgl/Kconfig"
#       报: "syntax error" + 'unknown option "osource"'
#
#   (2) 文件末行缺少换行符
#       kconfig-conf 的 lexer 在 EOF 处会把最后一个 token 记到父文件头上,
#       于是抛出一堆【跨文件】假报错 —— 被 source 的文件自身 if/endif 是
#       配平的, 只是末尾少了一个 \n:
#           arch/arm/src/sf32lb52/Kconfig:71: 'endif' in different file than 'if'
#           boards/arm/sf32lb52/Kconfig:28:  'endif' in different file than 'if'
#           Kconfig:2870: 'endmenu' in different file than 'menu'
#       本树命中:
#           arch/arm/src/sf32lb52/Kconfig        (本仓库文件, 仓库侧也已修正)
#           boards/arm/sf32lb52/Kconfig          (本仓库文件, 仓库侧也已修正)
#           drivers/hwtracing/tricoreht/Kconfig  (openvela 上游文件)
#
#   (3) `--help--` (比 `---help---` 少了开头一个连字符)
#       kconfiglib 是"意外宽容"地接受了它 (kconfiglib.py 里就有这条注释),
#       kconfig-conf 不认, 报 'unknown option "--help--"'。
#       本树命中: arch/tricore/Kconfig
#
#   三类都修掉后, configure.sh / make 开头的
#       make: *** [tools/Unix.mk:726：olddefconfig] 错误 1
#       ERROR: failed to refresh                            (tools/sethost.sh:231)
#   才会消失; 否则 .config 残缺 -> CONFIG_CROSS_COMPILE 为空 ->
#   退回主机 gcc -> "gcc: error: unrecognized command-line option '-mfloat-abi=soft'"。
#
#   【关于"安装清单"】
#   (1)(3) 与 (2) 里的 tricoreht 都是 openvela 上游文件, 不在本仓库里,
#   因此不存在"仓库源文件"可改, 只能在这里就地修 —— 本步骤即其安装清单。
#   (2) 里那两个本仓库文件 (arch/boards 的 Kconfig) 已在仓库源文件里补好
#   末行换行, 这里再兜一层, 防止复制进树后又被写坏。
#
#   幂等: 三种修法都不会重复施加。
# ------------------------------------------------------------
echo "[4d] 归一化 Kconfig 语法 (kconfig-frontends 兼容) ..."

KCONFIG_FIX_COUNT=0

# 4d-1. 末行补换行
while IFS= read -r _kc; do
    [ -n "$_kc" ] || continue
    [ -s "$_kc" ] || continue
    if [ "$(tail -c 1 "$_kc" | wc -l)" -eq 0 ]; then
        printf '\n' >> "$_kc"
        echo "  [FIX]  末行补换行: $_kc"
        KCONFIG_FIX_COUNT=$((KCONFIG_FIX_COUNT + 1))
    fi
done < <(find "$NUTTX_DIR" "$APPS_DIR" -name 'Kconfig*' -type f 2>/dev/null)

# 4d-2. --help-- -> ---help---
while IFS= read -r _kc; do
    [ -n "$_kc" ] || continue
    sed -i 's/^\([[:space:]]*\)--help--[[:space:]]*$/\1---help---/' "$_kc"
    echo "  [FIX]  --help-- -> ---help---: $_kc"
    KCONFIG_FIX_COUNT=$((KCONFIG_FIX_COUNT + 1))
done < <(grep -rlE -- '^[[:space:]]*--help--[[:space:]]*$' --include='Kconfig*' \
             "$NUTTX_DIR" "$APPS_DIR" 2>/dev/null || true)

# 4d-3. osource / orsource -> source (目标存在) 或注释掉 (目标不存在)
_fix_osource() {
    local kc="$1" tmp="$1.integrate.tmp" line indent inc real
    : > "$tmp"
    while IFS= read -r line || [ -n "$line" ]; do
        if [[ "$line" =~ ^([[:space:]]*)(osource|orsource)[[:space:]] ]]; then
            indent="${BASH_REMATCH[1]}"
            inc="${line#*\"}"
            inc="${inc%%\"*}"
            if [ -z "$inc" ] || [ "$inc" = "$line" ]; then
                printf '%s\n' "$line" >> "$tmp"
                continue
            fi
            real="${inc//\$APPSDIR/$APPS_DIR}"
            real="${real//\$APPSBINDIR/$APPS_DIR}"
            if [ -f "$real" ]; then
                printf '%ssource "%s"\n' "$indent" "$inc" >> "$tmp"
                echo "  [FIX]  $kc: osource -> source \"$inc\" (目标存在)"
            else
                printf '%s# [integrate] 目标不存在, 原样停用: %s\n' "$indent" "$line" >> "$tmp"
                echo "  [FIX]  $kc: osource 目标 $real 不存在 -> 该行已注释"
            fi
            KCONFIG_FIX_COUNT=$((KCONFIG_FIX_COUNT + 1))
        else
            printf '%s\n' "$line" >> "$tmp"
        fi
    done < "$kc"
    mv -f "$tmp" "$kc"
}

while IFS= read -r _kc; do
    [ -n "$_kc" ] || continue
    _fix_osource "$_kc"
done < <(grep -rlE -- '^[[:space:]]*(osource|orsource)[[:space:]]' --include='Kconfig*' \
             "$NUTTX_DIR" "$APPS_DIR" 2>/dev/null || true)

if [ "$KCONFIG_FIX_COUNT" -eq 0 ]; then
    echo "  [OK]   三类语法问题均不存在, 无需修改"
fi

# ------------------------------------------------------------
# 4e. LVGL 离线构建 —— 让 apps/graphics/lvgl 不再联网下载 v9.2.1.zip
#
#   【下载机制】apps/graphics/lvgl/Makefile:
#      :92   CONFIG_GRAPH_LVGL_URL ?= "https://github.com/lvgl/lvgl/archive/refs/tags"
#      :94   LVGL_VERSION = 9.2.1
#      :95   LVGL_TARBALL = v$(LVGL_VERSION).zip
#      :97   LVGL_UNPACKNAME = lvgl
#      :99   CURL ?= curl -L -O
#    :103-106 $(LVGL_TARBALL):      -> echo "Downloading: ..."; $(CURL) $(URL)/$(TARBALL)
#    :108-113 $(LVGL_UNPACKNAME):   -> unzip; mv lvgl-9.2.1 lvgl; touch lvgl
#    :116-118 ifeq ($(wildcard lvgl/.git),)  context:: lvgl    <- 下载的唯一触发点
#    :122-126 ifeq ($(wildcard lvgl/.git),)  distclean:: DELDIR lvgl / DELFILE zip
#
#   【判断条件】"已存在" = $(wildcard $(APPSDIR)/graphics/lvgl/lvgl/.git) 是否【非空】,
#   它【不是】检查 lvgl/lvgl.h。解压出来的源码里没有 .git, 于是:
#     * 每次 `context` 都会把 lvgl 追加为依赖 (只要 lvgl 比 zip 旧就重新解压);
#     * `make distclean` 会把 lvgl/ 与 v9.2.1.zip 一起删掉;
#     * 下一次 make 找不到 .git -> 必然重新联网 -> "Downloading: v9.2.1.zip"。
#   这就是联网下载的根因 (distclean 之后再 make 必现)。
#
#   【做法】不改任何上游 Makefile:
#     (1) 把精确 v9.2.1 源码放到 apps/graphics/lvgl/lvgl (与解压后目录结构一致);
#     (2) 在 lvgl/ 内建一个 .git 标记目录, 使上面的 wildcard 判断成立 ——
#         一举同时关闭"下载"与"distclean 删除"。幂等: 已存在则跳过。
# ------------------------------------------------------------
echo "[4e] 配置 LVGL 离线构建 (跳过 v9.2.1.zip 下载) ..."

LVGL_APP_DIR="$APPS_DIR/graphics/lvgl"
LVGL_SRC_DIR="$LVGL_APP_DIR/lvgl"
LVGL_WANT_VERSION="9.2.1"

_lvgl_version_of() {
    local f="$1/lv_version.h"
    [ -f "$f" ] || { echo ""; return; }
    awk '/LVGL_VERSION_MAJOR/{a=$3}/LVGL_VERSION_MINOR/{b=$3}/LVGL_VERSION_PATCH/{c=$3}
         END{if (a != "") printf "%s.%s.%s\n", a, b, c}' "$f"
}

# 4e-1. 确保源码就位 (优先复用已存在的精确版本)
if [ -f "$LVGL_SRC_DIR/lvgl.h" ]; then
    _cur="$(_lvgl_version_of "$LVGL_SRC_DIR")"
    if [ "$_cur" = "$LVGL_WANT_VERSION" ]; then
        echo "  [OK]   已存在 LVGL 源码: $LVGL_SRC_DIR (version $_cur)"
    else
        echo "  [WARN] 现有 LVGL 版本为 '$_cur' (期望 $LVGL_WANT_VERSION), 保持现状不覆盖"
    fi
else
    echo "  [..]   缺少 LVGL 源码, 尝试用本地压缩包离线解压 ..."
    _zip=""
    for _c in \
        "$LVGL_APP_DIR/v$LVGL_WANT_VERSION.zip" \
        "$SCRIPT_DIR/lvgl_offline/v$LVGL_WANT_VERSION.zip" \
        "/tmp/v$LVGL_WANT_VERSION.zip.bak"; do
        if [ -f "$_c" ]; then _zip="$_c"; break; fi
    done
    if [ -z "$_zip" ]; then
        echo "  [FAIL] 未找到本地 LVGL 压缩包, 无法离线放置源码。"
        echo "         请把官方 v$LVGL_WANT_VERSION.zip 放到:"
        echo "           $LVGL_APP_DIR/v$LVGL_WANT_VERSION.zip"
        echo "         然后重新运行 integrate.sh"
        exit 1
    fi
    echo "  [..]   使用本地压缩包: $_zip"
    _tmp="$LVGL_APP_DIR/.lvgl_unpack_tmp"
    rm -rf "$_tmp" && mkdir -p "$_tmp"
    if unzip -q -o "$_zip" -d "$_tmp"; then
        _root="$(find "$_tmp" -maxdepth 1 -mindepth 1 -type d -name 'lvgl-*' | head -n1)"
        if [ -n "$_root" ] && [ -f "$_root/lvgl.h" ]; then
            rm -rf "$LVGL_SRC_DIR"
            mv "$_root" "$LVGL_SRC_DIR"
            rm -rf "$_tmp"
            echo "  [OK]   已离线解压 LVGL -> $LVGL_SRC_DIR (version $(_lvgl_version_of "$LVGL_SRC_DIR"))"
        else
            rm -rf "$_tmp"
            echo "  [FAIL] 压缩包内未找到 lvgl-* 源码目录"
            exit 1
        fi
    else
        rm -rf "$_tmp"
        echo "  [FAIL] 解压失败: $_zip"
        exit 1
    fi
fi

# 4e-2. 建立 .git 离线标记 (幂等), 使 Makefile 的 wildcard 判断成立
if [ -d "$LVGL_SRC_DIR" ] && [ ! -e "$LVGL_SRC_DIR/.git" ]; then
    mkdir -p "$LVGL_SRC_DIR/.git"
    echo "  [FIX]  建立离线标记: $LVGL_SRC_DIR/.git (关闭下载 + 保护 distclean 不删源码)"
fi
if [ -e "$LVGL_SRC_DIR/.git" ]; then
    echo "  [OK]   LVGL 离线标记就绪, make 不会再触发 Downloading"
fi

# 4e-3. LVGL 空属性编译兼容 (与下载无关, 但同一批生效才能 make 成功)
#
#   【现象】make 在编译 lvgl 的字体/图像资源 .c 时报:
#       lv_conf_internal.h:1315:40: error: expected identifier or '('
#                                    before string constant
#       error: 'glyph_bitmap' undeclared here (not in a function)
#   命中 src/font/*.c 与 demos|examples/**/assets/**.c。
#
#   【根因】Kconfig 里 LV_ATTRIBUTE_MEM_ALIGN / LV_ATTRIBUTE_LARGE_CONST 是
#   string 型、default ""; NuttX 的 mkconfig 把 string 配置输出为
#   【带引号】的宏 (include/nuttx/config.h):
#       #define CONFIG_LV_ATTRIBUTE_MEM_ALIGN ""
#   lv_conf_internal.h:1315/1324 又把它直接当"声明属性"展开:
#       #define LV_ATTRIBUTE_MEM_ALIGN CONFIG_LV_ATTRIBUTE_MEM_ALIGN
#   于是 const 数组声明畸变成:
#       static "" const uint8_t glyph_bitmap[] = { ... }
#   => 编译失败。注意【改 Kconfig 默认值为其它字符串也没用】(仍带引号),
#      唯一正确的是让宏"为空", 而不是"等于某个字符串"。
#
#   【修法】在 LVGL 的 CFLAGS 里用 -D 抢占定义。lv_conf_internal.h 外层是
#   `#ifndef LV_ATTRIBUTE_MEM_ALIGN`, 一旦命令行已定义(即便为空), 那段
#   `#define ... CONFIG_...` 就被跳过, 宏保持为空 —— 正是 LVGL 的本意。
#   幂等: 带标记行则跳过。
#
#   放在 apps/graphics/lvgl/Makefile 里最稳: 它只作用于 LVGL 的编译单元,
#   不改板级/全局 CFLAGS, 也不动 lvgl 源码。distclean 不会删这个文件,
#   但 openvela 若同步上游会覆盖它, 故每次 integrate 重新校一遍。
LVGL_MK="$LVGL_APP_DIR/Makefile"
LVGL_FIX_TAG="# [integrate] LVGL empty-attribute fix"
if [ -f "$LVGL_MK" ] && ! grep -qF "$LVGL_FIX_TAG" "$LVGL_MK"; then
    awk -v tag="$LVGL_FIX_TAG" '
      !done && /^include \$\(APPDIR\)\/Application.mk/ {
          print tag
          print "CFLAGS   += \"-DLV_ATTRIBUTE_MEM_ALIGN=\" \"-DLV_ATTRIBUTE_LARGE_CONST=\""
          print "CXXFLAGS += \"-DLV_ATTRIBUTE_MEM_ALIGN=\" \"-DLV_ATTRIBUTE_LARGE_CONST=\""
          print ""
          done = 1
      }
      { print }
    ' "$LVGL_MK" > "$LVGL_MK.tmp" && mv "$LVGL_MK.tmp" "$LVGL_MK"
    echo "  [FIX]  LVGL 空属性兼容 CFLAGS 已注入 -> $LVGL_MK"
else
    echo "  [OK]   LVGL 空属性兼容 CFLAGS 已存在"
fi

# ------------------------------------------------------------
# 4e-4. 确保 LVGL 的 Kconfig 被真正 source (必须走 Kconfig 配置分支)
#
#   【这一行决定 LVGL 的配置来源】
#     apps/graphics/lvgl/Kconfig:28
#       source "$APPSDIR/graphics/lvgl/lvgl/Kconfig"
#   它一旦不生效, LVGL 的 Kconfig 符号就进不了 .config:
#     CONFIG_LV_CONF_SKIP / CONFIG_LVGL_VERSION_MAJOR|MINOR|PATCH 全部缺失,
#   于是复现本次要修的编译失败:
#     lv_conf_kconfig.h:27   "CONFIG_LVGL_VERSION_MAJOR" is not defined, evaluates to 0
#     lv_conf_kconfig.h:29   #warning "Version mismatch between Kconfig and lvgl/lv_version.h"
#     lv_conf_internal.h:50  因无 LV_CONF_SKIP 而进入包含分支
#     lv_conf_internal.h:60  #include "../../lv_conf.h" -> 不存在 -> fatal error
#
#   【为什么会不生效 —— 顺序缺陷】
#   上游该行写的是 `osource` (Linux5/kconfiglib 才支持), [4d-3] 负责把它归一化
#   成 `source`, 但 [4d] 在 [4e] 之前执行, 且 [4d-3] 只处理"未注释"的行。
#   若某次运行时 lvgl 源码尚未就位, [4d-3] 会把它改写成
#       # [integrate] 目标不存在, 原样停用: osource "$APPSDIR/..."
#   之后即便 [4e] 把源码解压/就位好了, 下一轮 [4d-3] 也不再匹配
#   (该行已带 `#`), 这行就【永久停用】-> 每次 configure 出来的 .config
#   都缺 LVGL 配置 -> 必然编译失败。
#   所以必须在 [4e](源码就位)之后做一次"反向自愈", 才能同轮修好。
#
#   幂等: 已是 `source` 时两条 sed 都不命中, 文件不变。
# ------------------------------------------------------------
LVGL_KC="$LVGL_APP_DIR/Kconfig"
if [ -f "$LVGL_SRC_DIR/Kconfig" ] && [ -f "$LVGL_KC" ]; then
    sed -i \
        -e 's|^\([[:space:]]*\)osource[[:space:]]*"\$APPSDIR/graphics/lvgl/lvgl/Kconfig"|\1source "$APPSDIR/graphics/lvgl/lvgl/Kconfig"|' \
        -e 's|^\([[:space:]]*\)#.*osource[[:space:]]*"\$APPSDIR/graphics/lvgl/lvgl/Kconfig".*|\1source "$APPSDIR/graphics/lvgl/lvgl/Kconfig"|' \
        "$LVGL_KC"

    if grep -qE '^[[:space:]]*source[[:space:]]+"\$APPSDIR/graphics/lvgl/lvgl/Kconfig"' "$LVGL_KC"; then
        echo "  [OK]   LVGL Kconfig source 行有效 (走 Kconfig 配置分支)"
    else
        echo "  [FAIL] $LVGL_KC 中找不到有效的 source \"\$APPSDIR/graphics/lvgl/lvgl/Kconfig\""
        echo "         -> CONFIG_LV_CONF_SKIP / CONFIG_LVGL_VERSION_* 不会进入 .config"
        exit 1
    fi
else
    echo "  [WARN] 跳过 LVGL Kconfig source 行自愈 (lvgl 源码或 Kconfig 缺失)"
fi

# ------------------------------------------------------------
# 5. 集成后校验
# ------------------------------------------------------------
echo "[5/6] 校验集成结果 ..."
FAIL=0

check() {
    if [ -e "$1" ]; then
        echo "  [OK]   $2"
    else
        echo "  [MISS] $2"
        FAIL=1
    fi
}

check "$NUTTX_DIR/arch/arm/src/sf32lb52/sf32lb52_start.c"            "芯片启动"
check "$NUTTX_DIR/arch/arm/src/sf32lb52/sf32lb52_serial.c"           "串口驱动"
check "$NUTTX_DIR/arch/arm/src/sf32lb52/sf32lb52_lowputc.c"          "低层输出"
check "$NUTTX_DIR/arch/arm/src/sf32lb52/sf32lb52_usart.h"            "USART 寄存器头"
check "$NUTTX_DIR/arch/arm/src/sf32lb52/Make.defs"                   "芯片 Make.defs"
check "$NUTTX_DIR/arch/arm/src/sf32lb52/Kconfig"                     "芯片 Kconfig"
check "$NUTTX_DIR/boards/arm/sf32lb52/Kconfig"                       "板级 Kconfig 入口"
check "$NUTTX_DIR/arch/arm/include/sf32lb52/irq.h"                   "芯片 arch irq.h (NR_IRQS)"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/include/board.h"  "板级 board.h"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/scripts/flash.ld" "链接脚本"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/scripts/Make.defs" "板级 Make.defs"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/configs/thermo/defconfig" "thermo defconfig"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/src/Makefile"     "板级 src/Makefile"
check "$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/src/libsf32lb52_vendorlcd.a" "厂家 LCD 静态库"
check "$APPS_DIR/thermo_panel/Makefile"                              "应用 Makefile"
check "$APPS_DIR/thermo_panel/thermo_main.c"                         "应用入口"
check "$APPS_DIR/graphics/lvgl/lvgl/lvgl.h"                          "LVGL 源码 (离线就位)"
check "$APPS_DIR/graphics/lvgl/lvgl/.git"                            "LVGL 离线标记 (.git, 关闭下载)"

# ------------------------------------------------------------
# Kconfig 语法复查 (对应 [4d])
#   这三类语法只要残留一处, kconfig-conf 就会让 olddefconfig 退出 1,
#   现象是 make 开头 "ERROR: failed to refresh", 进而 .config 残缺、
#   CONFIG_CROSS_COMPILE 为空、退回主机 gcc。
# ------------------------------------------------------------
if grep -rqE -- '^[[:space:]]*(osource|orsource)[[:space:]]' --include='Kconfig*' \
        "$NUTTX_DIR" "$APPS_DIR" 2>/dev/null; then
    echo "  [FAIL] 仍有 Kconfig 使用 osource (kconfig-conf 解析失败)"
    grep -rnE -- '^[[:space:]]*(osource|orsource)[[:space:]]' --include='Kconfig*' \
        "$NUTTX_DIR" "$APPS_DIR" | sed 's/^/         /'
    FAIL=1
else
    echo "  [OK]   Kconfig 无 osource (kconfig-frontends 兼容)"
fi

if grep -rqE -- '^[[:space:]]*--help--[[:space:]]*$' --include='Kconfig*' \
        "$NUTTX_DIR" "$APPS_DIR" 2>/dev/null; then
    echo "  [FAIL] 仍有 Kconfig 使用 --help-- (kconfig-conf 解析失败)"
    grep -rnE -- '^[[:space:]]*--help--[[:space:]]*$' --include='Kconfig*' \
        "$NUTTX_DIR" "$APPS_DIR" | sed 's/^/         /'
    FAIL=1
else
    echo "  [OK]   Kconfig 无 --help-- (kconfig-frontends 兼容)"
fi

_NOKEOL=0
while IFS= read -r _kc; do
    [ -n "$_kc" ] || continue
    [ -s "$_kc" ] || continue
    if [ "$(tail -c 1 "$_kc" | wc -l)" -eq 0 ]; then
        echo "  [FAIL] Kconfig 末行缺换行 (会报 'in different file than'): $_kc"
        _NOKEOL=1
    fi
done < <(find "$NUTTX_DIR" "$APPS_DIR" -name 'Kconfig*' -type f 2>/dev/null)
if [ "$_NOKEOL" -eq 0 ]; then
    echo "  [OK]   所有 Kconfig 均以换行符结尾"
else
    FAIL=1
fi

# ------------------------------------------------------------
# 库成员/符号级校验: 参数存储(NVS) 与 PWM 风机 的 .o 必须真的进了库。
#
#   为什么必须在这里查:
#     flash.ld 的 SRAM 常驻规则是按【成员名】匹配的
#       *bf0_hal_mpi.o *bf0_hal_mpi_ex.o *flash_table.o *bf0_vendor_nvs.o
#     如果库里根本没有这些成员, 规则就是空匹配 —— 编译照样成功, 但
#     QSPI2 擦/写期间代码仍在 flash 里取指, 上板才会跑飞 (最难查的一类)。
#     板级 Makefile 的 VENDOR_LCD_OBJS 也要能抽到这些成员, 否则 ar 报
#     "没有那个文件或目录"。
# ------------------------------------------------------------
VLIB="$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/src/libsf32lb52_vendorlcd.a"
if [ -f "$VLIB" ]; then
    VLIB_MEMBERS="$(arm-none-eabi-ar t "$VLIB" 2>/dev/null || true)"
    for m in bf0_hal_mpi.o bf0_hal_mpi_ex.o flash_table.o \
             bf0_vendor_fan.o bf0_vendor_nvs.o \
             bf0_vendor_pid.o; do
        if echo "$VLIB_MEMBERS" | grep -qx "$m"; then
            echo "  [OK]   库成员 $m"
        else
            echo "  [MISS] 库成员 $m (SRAM 常驻规则/板级 Makefile 会失效)"
            FAIL=1
        fi
    done

    VLIB_SYMS="$(arm-none-eabi-nm --defined-only "$VLIB" 2>/dev/null || true)"
    for s in sf32lb52_fan_init sf32lb52_fan_set_duty sf32lb52_fan_selftest \
             sf32lb52_nvs_init sf32lb52_nvs_load sf32lb52_nvs_save \
             sf32lb52_nvs_selftest sf32lb52_flash_delay_us \
             sf32lb52_nvs_set_cur_temp sf32lb52_nvs_get_cur_temp \
             sf32lb52_pid_step sf32lb52_pid_selftest \
             sf32lb52_setpoint_led_init sf32lb52_setpoint_led_set \
             sf32lb52_heat_pair_apply sf32lb52_heat_pair_selftest \
             sf32lb52_touch_vendor_selftest \
             sf32lb52_lcd_vendor_blit \
             sf32lb52_lcd_vendor_readpixel_selftest \
             sf32lb52_lcd_vendor_readpixel_usable \
             sf32lb52_lcd_vendor_solidtest; do
        # 注: sf32lb52_ui_* (UI 入口) 已不在本厂家库 ——
        #     UI 改为照厂家移植到 apps/thermo_panel (libapps.a)。
        if echo "$VLIB_SYMS" | grep -q "[TtWw] $s\$"; then
            echo "  [OK]   库符号 $s"
        else
            echo "  [MISS] 库符号 $s"
            FAIL=1
        fi
    done
fi

if [ "$FAIL" -ne 0 ]; then
    echo ""
    echo "ERROR: 集成校验失败, 请检查上游文件是否存在"
    exit 1
fi

# 内容级校验: 板级 scripts/Make.defs 会被 configure.sh 复制为顶层
# nuttx/Make.defs (它就是整个构建的顶层 Make.defs), 必须包含完整工具链配置,
# 否则 configure 阶段会报 "nuttx/Make.defs:1: *** 缺失分隔符"。

BOARD_MK="$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/scripts/Make.defs"
if grep -q 'tools/Config.mk' "$BOARD_MK" && grep -q 'Toolchain.defs' "$BOARD_MK"; then
    echo "  [OK]   板级 Make.defs 内容完整 (工具链配置)"
else
    echo "  [FAIL] 板级 scripts/Make.defs 缺少工具链配置"
    echo "         必须 include tools/Config.mk 与 arch/arm/src/armv8-m/Toolchain.defs"
    exit 1
fi

echo ""
echo "============================================"
echo "  集成完成"
echo ""
echo "  下一步 (配置并编译):"
echo "    cd $NUTTX_DIR"
echo "    ./tools/configure.sh sf32lb52-lcd:thermo"
echo "    make -j\$(nproc)"
echo ""
echo "  产物: nuttx.bin  (烧录地址 0x12010000)"
echo "============================================"
# ===== M1 SELF-HEAL (fix_now2.sh) =====
# 共享目录(hgfs)与 VM 之间偶发版本不一致。这里在集成之后、编译之前做兜底:
#   先删掉不该存在的宏定义行, 再把残留的符号名换成字面量。
# 【不要】直接做无差别替换 —— 那会把宏定义行本身也改坏 (曾导致
#   `#define 400000ul 400000ul`, 报 "macro names must be identifiers")。
_SER="$NUTTX_DIR/arch/arm/src/sf32lb52/sf32lb52_serial.c"
if [ -f "$_SER" ]; then
    if grep -q "400000ul\|SF32LB52_TXE_WAIT_MAX" "$_SER" 2>/dev/null; then
        sed -i -e '/^#define.*400000ul/d' \
               -e '/^#define.*SF32LB52_TXE_WAIT_MAX/d' \
               -e 's/SF32LB52_TXE_WAIT_MAX/400000ul/g' "$_SER" 2>/dev/null || true
    fi
fi
# ===== end M1 SELF-HEAL =====
