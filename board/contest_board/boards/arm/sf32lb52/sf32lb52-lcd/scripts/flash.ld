/****************************************************************************
 * boards/arm/sf32lb52/sf32lb52-lcd/scripts/flash.ld
 *
 * SF32LB52X NuttX 链接脚本 (XIP from QSPI2 NOR flash)
 *
 * 内存布局 (取自厂家工程的权威值):
 *   app   : 0x12218000, 0x240000 (2.25MB)  ER_IROM1 分区
 *   SRAM  : 0x20000000, 511KB  (__RAM_SIZE = 0x7FC00)
 *   顶端 1KB (0x2007FC00–0x2007FFFF): 厂家保留 (__ROM_EX_BASE)
 *
 * 烧录布局 (devkit_lcd_n16r8_firmware/sftool_param.json):
 *   ftab.bin        @ 0x12000000
 *   bootloader.bin  @ 0x12208000
 *   ER_IROM1.bin    @ 0x12218000   ← 本镜像 (nuttx.bin)
 *   厂家 ftab 中该分区: base=0x12218000 size=0x240000
 *
 * 堆栈划分:
 *   _ebss (BSS 末尾) → g_idle_topstack = _ebss + IDLE_STACKSIZE
 *   堆: 从 g_idle_topstack 到 0x2007FC00 (保留区前一位置)
 *   初始 MSP: _sstack = _mbox_base = 0x2007FC00, 向下生长
 *
 * 符号约定 (NuttX ARM 通用):
 *   _stext/_etext            代码段
 *   _sdata/_edata/_eronly    .data 的 VMA 起止与 flash 侧 LMA
 *   _sbss/_ebss              .bss 起止
 *   _sstack/_estack          初始栈顶
 ****************************************************************************/

ENTRY(__start)

MEMORY
{
  flash (rx)  : ORIGIN = 0x12218000, LENGTH = 0x240000

  /* 【重要】RAM 起点必须 ≥ 0x20002000
   *
   * 上板实测 (probe4 / probe5):
   *   向量表 MSP = 0x20002000, 全程不压栈   -> 正常输出
   *   向量表 MSP = 0x20001E48, 第一条 push  -> 一个字符都没有
   *   NuttX 原布局 MSP = _ebss+2048 = 0x20001E48, __start 第一条就是
   *   push {r3,r4,r5,lr} (机器码 38 B5)     -> 同样静默
   *
   * 厂家链接脚本把 .stack 放在 0x20000000~0x20002000 且 __StackTop
   * 正好 = 0x20002000, 说明 ROM/bootloader 给 ARMv8-M 的 MSPLIM
   * (栈下限寄存器) 设了一个不低于 0x20002000 的值, 只要压栈越过它
   * 就直接栈越界异常 —— 而 NuttX 官方 arm_m/arm_head.S 里清 MSPLIM
   * 的那几行, 在本移植里没有编进去。
   *
   * 把 RAM 起点抬到 0x20002000 后, .data/.bss/空闲栈全部位于该值之上,
   * 压栈不会越限。后续再加汇编 stub 清 MSPLIM 做双保险。
   */

  sram  (rwx) : ORIGIN = 0x20002000, LENGTH = 0x7DC00

  /* 厂家 PSRAM 数据区 (厂家 main.map: PSRAM 0x60200000 0x00600000 rw;
   * __PSRAM_BASE = 0x60200000)。只取头部 1MB 给 LCD 取数缓冲用, 不碰其余。
   * 依据: 厂家 LVGL 绘制缓冲放在 PSRAM
   *       (厂家 lv_lcd.c:177-179 L2_NON_RET_BSS_SECT(frambuf, buf2_1);
   *        厂家 main.map .bss.l2_non_ret_bss_frambuf @ 0x60311700)。 */
  psram (rw)  : ORIGIN = 0x60200000, LENGTH = 0x00100000
}

/* SRAM 顶端减去 mailbox 区 (1KB) */

_sram_top  = ORIGIN(sram) + LENGTH(sram);
_mbox_base = _sram_top - 0x400;          /* 0x2007FC00 */

/* 注意: 初始 MSP 并不由 _sstack 决定。
 * arm_m/arm_vectors.c 使用:
 *   #define IDLE_STACK (_ebss + CONFIG_IDLETHREAD_STACKSIZE)
 * 即 MSP 起点 = _ebss + IDLETHREAD_STACKSIZE, 与芯片层
 * sf32lb52_allocateheap.c 的 g_idle_topstack 取值一致:
 *   栈 : [_ebss, _ebss + IDLETHREAD_STACKSIZE)  向下生长
 *   堆 : [_ebss + IDLETHREAD_STACKSIZE, 0x2007FC00) 向上生长
 * 下面两个符号仅作记录, 未被 NuttX 引用。
 */

_sstack    = _mbox_base;
_estack    = _mbox_base;

SECTIONS
{
  /* ======================================================================
   * 1) 向量表: 必须是镜像首地址 (VTOR 指向本分区首字节)
   *
   *    单独成一个输出段, 是为了让下面的 .ramdata 能"插在它后面"、
   *    又仍然位于 .text 通配之前 (见 .ramdata 里的说明)。
   * ==================================================================== */

  .vectors :
  {
    _stext      = ABSOLUTE(.);

    _svectors   = ABSOLUTE(.);
    KEEP(*(.vectors))
    _evectors   = ABSOLUTE(.);
  } > flash

  /* ======================================================================
   * 2) SRAM 常驻段: 厂家 QSPI NOR 编程栈 + 初始化数据
   *
   *  ★ 为什么厂家 QSPI 栈必须常驻 SRAM:
   *      本固件自己就从 QSPI2 (0x12000000) XIP 取指, 而参数也写在 QSPI2。
   *      擦/写期间那片 NOR 无法响应取指 —— 代码若还在 flash 里跑就会跑飞。
   *      厂家做法同源: 厂家 link.lds 的 .retm_data 段把
   *        drv_spi_flash.o / flash_table.o / bf0_hal_mpi.o / bf0_hal_mpi_ex.o
   *      由 `> RAM AT > ROM` 搬到 RAM
   *      (app/project/sf32lb52-xty-ai_hcpu/link.lds:153-166)。
   *
   *  ★ 为什么这一段必须【声明在 .text 之前】:
   *      ld 按 SECTIONS 的声明顺序分配输入段。若 .text 的
   *      `*(.text .text.*)` 先出现, 它会把 *bf0_hal_mpi.o 的 .text 一并
   *      吃掉, 后面再写这些规则就是空匹配。
   *      实测: 本工具链 (binutils 2.42) 的 EXCLUDE_FILE 对通配名与精确名
   *      【都不生效】(已用最小工程验证), 所以只能靠声明顺序 ——
   *      厂家 link.lds 的 .retm_data 也正是这个套路 (它排在通用 .text 之前)。
   *      务必【不要】把本段移到 .text 之后。
   *
   *  ★ 为什么紧接着还要放 .data, 且中间不能夹别的段:
   *      启动拷贝循环是 "dest(_sdata.._edata) <== src(_eronly..)" 的顺序拷贝,
   *      要求 [_sdata, _edata) 的 VMA 与以 _eronly 起的 LMA 一一对应。
   *      .ramdata 与 .data 都用 `> sram AT > flash` 且声明相邻, VMA 与 LMA
   *      双双连续, 于是同一个拷贝循环就能把两者一起搬到 SRAM。
   *      若在两者之间再夹一个 `> flash` 的输出段 (例如 .text), 地址就不再
   *      对应, 拷贝会拷进代码的加载镜像 -> 必然跑飞。
   *
   *  ★ 名字匹配 (板级 src/ 里的对象是 v<成员>.o):
   *      *bf0_hal_mpi.o    匹配 vbf0_hal_mpi.o, 不匹配 vbf0_hal_mpi_ex.o
   *      *bf0_hal_mpi_ex.o 匹配 vbf0_hal_mpi_ex.o
   *      *flash_table.o    匹配 vflash_table.o (命令表在 .rodata, 也要常驻)
   *      *bf0_vendor_nvs.o 匹配 vbf0_vendor_nvs.o (含 sf32lb52_flash_delay_us)
   * ==================================================================== */

  .ramdata :
  {
    . = ALIGN(8);
    _sdata = ABSOLUTE(.);

    *bf0_hal_mpi.o    (.text .text.* .rodata .rodata.*)
    *bf0_hal_mpi_ex.o (.text .text.* .rodata .rodata.*)
    *flash_table.o    (.text .text.* .rodata .rodata.*)
    *bf0_vendor_nvs.o (.text .text.* .rodata .rodata.*)

    . = ALIGN(8);
    _eramdata = ABSOLUTE(.);
  } > sram AT > flash

  /* ---- 初始化数据 (VMA=SRAM, LMA=flash 紧跟常驻段之后) ----
   * 单独成段 (而不是并进 .ramdata) 是为了保留可写属性 SHF_WRITE。 */

  .data :
  {
    . = ALIGN(8);
    *(.data .data.*)
    . = ALIGN(8);
    _edata = ABSOLUTE(.);
  } > sram AT > flash

  /* 拷贝源地址 (NuttX 启动 "_eronly -> _sdata.._edata" 使用):
     必须取 .ramdata 的 LMA —— 它是 [_sdata, _edata) 这一段对应的 flash 首地址。 */

  _eronly = LOADADDR(.ramdata);

  /* ======================================================================
   * 3) 其余代码 + 只读数据 (flash, XIP)
   * ==================================================================== */

  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.glue_7)
    *(.glue_7t)
    . = ALIGN(8);
    _etext = ABSOLUTE(.);
  } > flash

  /* ======================================================================
   * 4) 异常索引表
   * ==================================================================== */

  .ARM.exidx :
  {
    *(.ARM.extab*)
    *(.ARM.exidx*)
    . = ALIGN(8);
  } > flash

  /* ======================================================================
   * 5) BSS (SRAM, 启动时清零)
   * ==================================================================== */

  .bss :
  {
    . = ALIGN(8);
    _sbss = ABSOLUTE(.);
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = ABSOLUTE(.);
  } > sram

  /* ======================================================================
   * 5b) LCD 取数缓冲 (PSRAM, 不占镜像, 不参与 SRAM BSS 清零)
   *
   *   照厂家: 厂家把 LVGL 绘制缓冲放在 PSRAM ——
   *     厂家 lv_lcd.c:177-179  L2_NON_RET_BSS_SECT(frambuf, ...)
   *     厂家 main.map: .bss.l2_non_ret_bss_frambuf @ 0x60311700
   *   本工程原来放在 .bss (> sram, 0x2000xxxx = RAM0/DTCM), LCDC 的
   *   layer 取数路径取不到 -> EOF 不来 -> 刷屏卡死。
   *
   *   NOLOAD: 只占地址, 不生成加载数据 (启动不清零, 由 C 侧显式 memset)。
   * ==================================================================== */

  .psram_lcdbuf (NOLOAD) :
  {
    . = ALIGN(64);
    _psram_lcdbuf_start = ABSOLUTE(.);
    *(.psram_lcdbuf)
    *(.psram_lcdbuf.*)
    . = ALIGN(64);
    _psram_lcdbuf_end = ABSOLUTE(.);
  } > psram

  /* ======================================================================
   * 6) 不放入镜像的段
   * ==================================================================== */

  /DISCARD/ :
  {
    *(.ARM.attributes)
    *(.comment)
    *(.note*)
  }
}