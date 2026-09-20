#!/bin/bash
# ============================================================
# SF32LB52 温控面板 OpenVela 一键构建脚本
#
# 在 Ubuntu 22.04 + openvela 源码环境下运行:
#   export OPENVELA_ROOT=~/openvela   # nuttx/ 和 apps/ 的父目录
#   bash build_openvela.sh
#
# 脚本完成: 集成 → 配置 → 编译 → 报告产物路径
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "============================================"
echo "  SF32LB52 Thermo Panel - OpenVela Build"
echo "  SCRIPT_DIR = $SCRIPT_DIR"
echo "============================================"

# 1. 检查环境变量
if [ -z "$OPENVELA_ROOT" ]; then
    echo "ERROR: OPENVELA_ROOT not set"
    echo "  export OPENVELA_ROOT=/path/to/openvela_parent"
    echo "  (OPENVELA_ROOT 下应包含 nuttx/ 和 apps/ 子目录)"
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

# 2. 清理旧配置
#
# 【顺序关键】distclean 必须在 integrate.sh【之前】!
#
#   integrate.sh 的 [2b] 步会构建 libsf32lb52_vendorlcd.a 并放到板级 src/。
#   make distclean 会把该目录下的 *.a / *.o 一并删除。
#   若 distclean 排在 integrate.sh 之后, 库就没了, 而
#       boards/.../scripts/Make.defs 在 CONFIG_SF32LB52_HAS_LCDC=y 时是
#       $(error 厂家 LCD 静态库缺失 ...)
#   于是 configure 阶段直接中断 —— 报错信息还很难懂。
echo "[1/4] 清理旧配置 (distclean + 陈旧符号链接)..."
cd "$NUTTX_DIR"

# configure.sh 要求源码树处于未配置状态, 否则报 "Already configured!"
if [ -e "$NUTTX_DIR/.config" ]; then
    echo "  检测到遗留 .config, 先执行 make distclean ..."
    make distclean >/dev/null 2>&1 || true
    rm -f "$NUTTX_DIR/.config"
fi

# 清理可能陈旧的符号链接!
# configure.sh 建 <arch>/board、<arch>/chip 这些链接时用的是 `ln -s`
# (不带 -f), 若目标已存在则【不会覆盖且静默失败】, 于是残留上一块板子
# (如 nrf53) 的头文件目录。
# 症状: <arch/board/board.h> 取到别家的 board.h ->
#       板级宏 undeclared (例如 BOARD_CONSOLE_BAUD 报错,
#       而 nrf53 也定义过的 BOARD_SYSTICK_CLOCK 反而不报)。
# 因此在 configure 之前强制删除, 让其重新建立。
rm -f "$NUTTX_DIR/include/arch/board"
rm -f "$NUTTX_DIR/include/arch/chip"
rm -f "$NUTTX_DIR/arch/arm/include/board"
rm -f "$NUTTX_DIR/arch/arm/include/chip"
rm -f "$NUTTX_DIR/arch/arm/src/board"
rm -f "$NUTTX_DIR/arch/arm/src/chip"

# 3. 集成 (复制芯片层/板级层/应用层 + 注册 Kconfig + 构建厂家 LCD 库)
echo "[2/4] 集成文件到 openvela 源码树..."
bash "$SCRIPT_DIR/integrate.sh"

# 4. 配置
echo "[3/4] 配置 (sf32lb52-lcd:thermo)..."
cd "$NUTTX_DIR"
./tools/configure.sh sf32lb52-lcd:thermo

# 3a2. 链接指向硬校验 (确保 board.h / chip.h 指向【我们】的目录)
echo "[2a/4] 校验符号链接指向 ..."
for lk in "$NUTTX_DIR/arch/arm/include/board" \
          "$NUTTX_DIR/arch/arm/include/chip" \
          "$NUTTX_DIR/arch/arm/src/chip"; do
    tgt="$(readlink -f "$lk" 2>/dev/null)"
    case "$tgt" in
        *sf32lb52*)
            echo "  [OK]   $(basename "$lk") -> $tgt" ;;
        "")
            echo "  [FAIL] $(basename "$lk") 不存在或无法解析"
            exit 1 ;;
        *)
            echo "  [FAIL] $(basename "$lk") -> $tgt (不是 sf32lb52!)"
            exit 1 ;;
    esac
done

# 3b1b. 用户入口点必须用 INIT_ENTRYPOINT (openvela 改名, USER_ENTRYPOINT 已不存在),
#       且必须指向 libapps.a 里真实存在的符号。
#
#   apps/Application.mk:365 会把 app 的 main 重命名:
#       -Dmain=$(PROGNAME)_main     ->  NSH 的入口成为 `nsh_main`
#   因此 libapps.a 中【没有】 main 符号 (nm 可验证)。
#   若 nx_bringup.c 去引用 main, 链接期即报:
#       undefined reference to `main'
if grep -q '^CONFIG_INIT_ENTRYPOINT="nsh_main"$' "$NUTTX_DIR/.config"; then
    echo '  [OK]   CONFIG_INIT_ENTRYPOINT="nsh_main"'
else
    echo "  [FAIL] CONFIG_INIT_ENTRYPOINT 未设为 nsh_main:"
    grep -nE '^CONFIG_(INIT|USER)_ENTRYPOINT' "$NUTTX_DIR/.config" | sed 's/^/         /'
    echo "         openvela 的符号名是 INIT_ENTRYPOINT; USER_ENTRYPOINT 会被 Kconfig 丢弃。"
    echo "         入口须指向 libapps.a 中真实存在的 nsh_main (app 的 main 被"
    echo "         Application.mk 重命名为 \$(PROGNAME)_main)。"
    exit 1
fi

# 3b. 配置结果硬校验
#     Kconfig 注册链若缺一环, 这些符号会被 Kconfig 静默丢弃 (不报错),
#     典型症状是 -mcpu 落到默认值, 汇编报
#       "selected processor does not support `mrs r5,basepri'"
#     因此必须在这里显式挡住。
echo "[2b/4] 校验 .config 关键符号 ..."
CONFIG_FAIL=0
for sym in ARCH_CHIP_SF32LB52 ARCH_BOARD_SF32LB52_LCD ARCH_CORTEXM33 ARCH_ARMV8M ARCH_ARM_M TIMER ARMV8M_SYSTICK; do
    if grep -q "^CONFIG_${sym}=y\$" "$NUTTX_DIR/.config"; then
        echo "  [OK]   CONFIG_${sym}=y"
    else
        echo "  [FAIL] CONFIG_${sym}=y 未生效 (Kconfig 注册链不完整)"
        CONFIG_FAIL=1
    fi
done

if [ "$CONFIG_FAIL" -ne 0 ]; then
    echo ""
    echo "ERROR: 关键符号未进入 .config, 编译必然失败。"
    echo "       请检查 integrate.sh 的 Kconfig 注册是否注入到了"
    echo "       arch/arm/Kconfig 与 boards/Kconfig (注意: 不是"
    echo "       arch/arm/src/Kconfig 与 boards/arm/Kconfig, 这两个文件不存在)。"
    exit 1
fi

# 3b2. 板卡选择必须真正生效。
#      若落回 ARCH_BOARD_CUSTOM, tools/Config.mk:168 会走自定义分支:
#        CUSTOM_DIR 为空 -> BOARD_DIR = $(TOPDIR)/
#        -> ARCHSCRIPT = nuttx//scripts/flash.ld
#      后果: "没有规则可制作目标 flash.ld" 以及 "没有规则可制作目标 libboard.a"
if grep -q '^CONFIG_ARCH_BOARD_CUSTOM=y$' "$NUTTX_DIR/.config"; then
    echo "  [FAIL] CONFIG_ARCH_BOARD_CUSTOM=y"
    echo "         板卡符号未进入 boards/Kconfig 的 choice, 板卡实际没被选中。"
    echo "         表现为 BOARD_DIR 退化为 \$(TOPDIR)/, 找不到链接脚本与 libboard.a。"
    exit 1
fi
echo "  [OK]   CONFIG_ARCH_BOARD_CUSTOM 未启用 (未走自定义板卡分支)"

# 3b2b. 家族目录必须解析为 armv8-m。
#   arch/arm/src/Makefile:34 的 if/else-if 链【先判断】ARCH_ARMV7M,
#   若它意外为 y (典型来源: 残留的 ARCH_CORTEXM3=y, 它会 select ARCH_ARMV7M),
#   则 ARCH_SUBDIR = armv7-m, VPATH 缺 armv8-m, 报:
#       make: 没有规则可制作目标 "arm_sau.c" (由 .depend 需求)
#   且 CONFIG_ARCH_FAMILY 会变成 "armv7-m"。
# 芯片 choice 必须真正生效, 否则落回默认的 ARCH_CHIP_STM32 分支。
#   arch/arm/Kconfig:55  choice / :57 default ARCH_CHIP_STM32 / :714 endchoice
#   落回后的完整因果链:
#     ARCH_CHIP_STM32 -> ARCH_CHIP_STM32F103ZE (Cortex-M3)
#     -> ARCH_CORTEXM3 -> ARCH_ARMV7M -> ARCH_FAMILY="armv7-m"
#     -> arch/arm/src/Makefile:34 先命中 ARMV7M -> ARCH_SUBDIR=armv7-m
#     -> VPATH 缺 armv8-m -> "没有规则可制作目标 arm_sau.c"
for bad in ARCH_CHIP_STM32 ARCH_CHIP_STM32F103ZE ARCH_CORTEXM3 ARCH_ARMV7M; do
    if grep -q "^CONFIG_${bad}=y\$" "$NUTTX_DIR/.config"; then
        echo "  [FAIL] CONFIG_${bad}=y —— 芯片 choice 未生效, 落回了默认 STM32 分支"
        echo "         请检查 integrate.sh 的 inject_chip_symbol 是否注入到"
        echo "         arch/arm/Kconfig 的 MCU choice 内部 (config ARCH_CHIP_NRF53 之前)。"
        exit 1
    fi
done

if grep -q '^CONFIG_ARCH_FAMILY="armv8-m"$' "$NUTTX_DIR/.config"; then
    echo '  [OK]   CONFIG_ARCH_FAMILY="armv8-m" (芯片 choice 生效)'
else
    echo "  [FAIL] CONFIG_ARCH_FAMILY 不是 armv8-m:"
    grep -n '^CONFIG_ARCH_FAMILY' "$NUTTX_DIR/.config" | sed 's/^/         /'
    exit 1
fi

# 3b3. 端到端探针: 用 make 展开 BOARD_DIR / ARCHSCRIPT, 确认指向我们板级目录
printf 'TOPDIR := %s\ninclude $(TOPDIR)/Make.defs\np:\n\t@echo "BOARD_DIR=[$(BOARD_DIR)]"\n\t@echo "ARCHSCRIPT=[$(ARCHSCRIPT)]"\n' \
       "$NUTTX_DIR" > /tmp/sf32lb52_probe.mk

if PROBE="$(make -f /tmp/sf32lb52_probe.mk p 2>/dev/null)"; then
    echo "$PROBE" | sed 's/^/  /'
    case "$PROBE" in
        *boards/arm/sf32lb52/sf32lb52-lcd*)
            echo "  [OK]   BOARD_DIR/ARCHSCRIPT 指向 sf32lb52-lcd" ;;
        *)
            echo "  [FAIL] BOARD_DIR/ARCHSCRIPT 未指向 sf32lb52-lcd"
            exit 1 ;;
    esac
else
    echo "  [WARN] BOARD_DIR 探针未能执行, 跳过该项检查"
fi

# 4. 编译
# 注意: 本工程源码树极大 (全部芯片/板级/应用), 默认 -j$(nproc) 会 fork 出
#       成千上万个子 make, 在虚拟机上极易触发
#       "/bin/sh: 资源暂时不可用" (EAGAIN) 而失败。
#       故默认限制为 4 路并行, 可用 MAKE_JOBS 覆盖。
JOBS="${MAKE_JOBS:-4}"
echo "[3/4] 编译 (make -j${JOBS})..."
make -j${JOBS}

# 3c. 生成扁平二进制。
#     本树的 tools/Unix.mk:569 只在显式请求该目标时才做 objcopy, 普通 make
#     只产出 ELF, 所以这里手工生成, 避免"忘了 objcopy"。
#
#     objcopy -O binary 按【LMA】排布, 因此:
#       .text      VMA=LMA=0x12218000 (flash)   -> 进镜像
#       .data      VMA=0x20000000 / LMA=flash   -> 按 LMA 放在 flash ✓
#       .bss       NOLOAD                        -> 不进镜像 ✓
#     不会出现"从 0x12218000 一直填到 0x20000000"的巨大空洞。
if command -v arm-none-eabi-objcopy >/dev/null 2>&1; then
    OBJCOPY=arm-none-eabi-objcopy
else
    OBJCOPY=objcopy
fi
echo "  [..]   ${OBJCOPY} -O binary nuttx nuttx.bin"
"${OBJCOPY}" -O binary "$NUTTX_DIR/nuttx" "$NUTTX_DIR/nuttx.bin"

# 3d. 产物硬校验: 大小必须小于 ER_IROM1 分区上限 0x240000 = 2359296
if [ -f "$NUTTX_DIR/nuttx.bin" ]; then
    BINSZ=$(stat -c%s "$NUTTX_DIR/nuttx.bin" 2>/dev/null || echo 0)
    if [ "$BINSZ" -le 0 ]; then
        echo "  [FAIL] nuttx.bin 大小为 0"
        exit 1
    elif [ "$BINSZ" -ge 2359296 ]; then
        echo "  [FAIL] nuttx.bin = ${BINSZ} 字节, 超过 ER_IROM1 分区上限 2359296"
        exit 1
    else
        echo "  [OK]   nuttx.bin = ${BINSZ} 字节 (< 2359296, 烧录地址 0x12218000)"
    fi
else
    echo "  [FAIL] nuttx.bin 未生成"
    exit 1
fi

# 5. 报告产物
echo "[OK] 编译完成, 产物清单:"
ls -lh "$NUTTX_DIR/nuttx.bin" 2>/dev/null || echo "  nuttx.bin 不存在"
ls -lh "$NUTTX_DIR/nuttx.hex" 2>/dev/null || echo "  nuttx.hex 不存在"

echo ""
echo "============================================"
echo "  烧录方法 (应用基址 0x12218000, ER_IROM1 分区):"
echo ""
echo "  方式一: 只替换应用镜像 (保留厂家 bootloader/ftab)"
echo "    把 nuttx.bin 复制为:"
echo "      devkit_lcd_n16r8_firmware/main.bin/ER_IROM1.bin"
echo "    然后用原 sftool_param.json 烧录即可。"
echo ""
echo "  方式二: 直接指定地址烧录"
echo "    sftool -p COMx -c SF32LB52 -m nor write_flash \\"
echo "      \"$NUTTX_DIR/nuttx.bin@0x12218000\""
echo ""
echo "  注意: 不要改动 ftab(0x12000000) 与 bootloader(0x12208000)，"
echo "        否则 bootloader 找不到应用。"
echo "============================================"
