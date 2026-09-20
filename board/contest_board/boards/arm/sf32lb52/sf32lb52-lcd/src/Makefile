############################################################################
# boards/arm/sf32lb52/sf32lb52-lcd/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   `http://www.apache.org/licenses/LICENSE-2.0`
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
# 参考实现: boards/arm/mps/mps3-an547/src/Makefile
#           boards/arm/s32k1xx/s32k118evb/src/Makefile
#
# 【必须】include $(TOPDIR)/boards/Board.mk, 而【不是】boards/Makefile!
#
#   * boards/Board.mk   → 板级库 libboard.a 的通用模板 (本文件要的)
#   * boards/Makefile   → 给 boards/ 目录构建 libboards.a 用的, 是另一个东西
#
#   误 include boards/Makefile 会命中它的 context 目标:
#       context:
#       ifeq ($(BOARD_INSTALLED),y)
#           $(Q) $(MAKE) -C $(BOARDDIR) context
#       endif
#   其中 BOARDDIR = $(TOPDIR)/arch/arm/src/board, 正是本目录 (软链接),
#   于是无限自我递归, 最终耗尽进程资源:
#       make[7645]: /bin/sh: 资源暂时不可用
#       make[N]: *** [boards/Makefile:81: context] 错误 2
############################################################################

include $(TOPDIR)/Make.defs

CSRCS = sf32lb52_lcd_bringup.c

include $(TOPDIR)/boards/Board.mk

############################################################################
# 厂家 LCD 栈: 把库成员直接并入 libboard.a
#
# 【为什么不能只靠 LDLIBS —— 实测踩过】
#   静态库链接对顺序敏感: ld 只在"当前存在未定义符号"时才回头扫描库。
#   libboard.a 里的 sf32lb52_lcd_bringup.o 引用了 sf32lb52_lcd_vendor_*,
#   而厂家库若排在 libboard.a 之前被扫过 (它自己没有任何未定义符号可解),
#   就会被跳过, 最终全部报 undefined reference:
#       undefined reference to `sf32lb52_lcd_vendor_init`
#       ... readid / fill / readpixel / setbrightness
#   并进同一个归档后, 符号在 libboard.a 内部自洽, 再无顺序依赖。
#
# 【成员名加 v 前缀】
#   避免与 NuttX 自己的 .o 撞名 (厂家库里有 bf0_hal_rcc.o 这种通用名)。
#
# 【自愈】
#   make clean 会删掉这些 .o (它们在 OBJS 里), 这里给了从 .a 重新抽取的
#   规则, 所以不需要重跑 integrate.sh。只有 distclean 会连 .a 一起删,
#   那时需要重跑 integrate.sh (它会重新构建 .a)。
############################################################################

VENDOR_LCD_LIB  = $(CURDIR)/libsf32lb52_vendorlcd.a
VENDOR_LCD_OBJS = vbf0_hal_lcdc.o vbf0_hal_rcc.o vbf0_hal_gpio.o \
                  vbf0_hal_pinmux.o vbf0_hal_tim.o vbf0_pin_const.o \
                  vbf0_hal_i2c.o vbf0_hal_hpaon.o vbf0_hal_adc.o \
                  vbf0_hal_mpi.o vbf0_hal_mpi_ex.o vflash_table.o \
                  vbf0_vendor_glue.o vbf0_vendor_touch.o vbf0_vendor_ntc.o \
                  vbf0_vendor_fan.o vbf0_vendor_nvs.o \
                  vbf0_vendor_pid.o vbf0_vendor_gpio.o

# 【注意】原先还有 vbf0_vendor_ui.o (手绘三屏 UI 胶水)。本次改为"照厂家移植":
#   UI 已换成厂家 app/src/thermo_ui.c 原文 + LVGL, 编在 apps/thermo_panel
#   (libapps.a) 里, 不再属于本厂家库。故从抽取清单中删除。

$(VENDOR_LCD_OBJS): $(VENDOR_LCD_LIB)
	$(Q) rm -rf $(CURDIR)/.vlcd
	$(Q) mkdir -p $(CURDIR)/.vlcd
	$(Q) cd $(CURDIR)/.vlcd && $(AR) x $(VENDOR_LCD_LIB)
	$(Q) cd $(CURDIR)/.vlcd && for f in *.o; do cp -f $$f $(CURDIR)/v$$f; done
	$(Q) rm -rf $(CURDIR)/.vlcd

OBJS += $(VENDOR_LCD_OBJS)

# 【必须】再把它们挂成 libboard.a 的前提!
#
#   GNU make 在【读取 Makefile 时】就展开并定型了规则的前提表:
#   Board.mk 的 `$(BIN): $(OBJS)` 早于上一行 `OBJS +=` 被解析, 所以
#       * recipe 运行时 $(OBJS) 才求值 -> ar 拿到了 vbf0_hal_lcdc.o
#       * 依赖图里却没有这个对象 -> make 不会执行上面的抽取规则
#   实测报错:
#       arm-none-eabi-ar: vbf0_hal_lcdc.o: 没有那个文件或目录
#       make[2]: *** [boards/Board.mk:97：libboard.a] 错误 1
#
#   这里再写一条【没有 recipe 的单冒号规则】—— make 会把前提并进
#   已有的 $(BIN) 规则 (无 recipe 的同名单冒号规则合法, 前提合并)。
$(BIN): $(VENDOR_LCD_OBJS)