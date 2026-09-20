/**
 * @file sf32lb52_lcd_hw.c
 * @brief SF32LB52X + CO5300 屏硬件层 (纯寄存器, 不依赖 NuttX 头文件)
 *
 * 【本文件为什么不能 include NuttX 头】
 *   本文件要调用移植进来的厂商 LCDC HAL (bf0_hal_lcdc.c)。厂商世界里
 *   register.h 定义了 `enum { ERROR = 0 }`, 而 NuttX 的 sys/types.h 定义
 *   `enum { ERROR = -1 }` ⇒ 同一个编译单元里两者共存会直接编译失败。
 *   因此本文件只走"厂商世界 + shim"(编译时把 sdk_port/shim 放在 -I 最前),
 *   对外只暴露几个纯 C 接口, 由 NuttX 侧的 sf32lb52_lcd_board.c 调用。
 *
 * 【硬件事实来源 (厂家 SDK, 均可追溯)】
 *   app/boards/sf32lb52-xty-ai_base/bsp_lcd.c
 *       LCD_RESET_PIN = 0      -> PA00 (GPIO)
 *       LCD_BL_PIN    = 42     -> PA42 (GPIO)
 *       BSP_LCD_PowerUp(): BL=1 -> RESET=1 -> 500us -> BSP_PIN_LCD()
 *   app/boards/sf32lb52-xty-ai_base/bsp_pinmux.c  BSP_PIN_LCD()
 *       PA03 = LCDC1_SPI_CS / PA04 = LCDC1_SPI_CLK
 *       PA05 = LCDC1_SPI_DIO0 / PA06 = LCDC1_SPI_DIO1
 *   app/boards/sf32lb52-xty-ai_base/Kconfig.board
 *       select BSP_LCDC_USING_SPI_DCX_1DATA   (1 数据线 + DCX)
 *   sdk/customer/peripherals/co5300/co5300.c
 *       LCDC 配置: freq 48MHz / RGB565 / spi{syn_mode=DISABLE, vsyn_polarity=1}
 *       CO5300 初始化序列 / 亮度 = 写屏寄存器 0x51 (0~255)
 */

#include "bf0_hal.h"

/* ---------------------------------------------------------------------------
 * 板级常量
 * ------------------------------------------------------------------------- */

#define LCD_RESET_PIN           0        /* PA00 */
#define LCD_BL_PIN              42       /* PA42 */
/* ★ 屏电源使能 LCD_VADD_EN: 厂家 sf32lb52-lcd_base/bsp_lcd_tp.c
 *     #define LCD_VADD_EN  (37)    // GPIO_A37
 *   并在 BSP_LCD_PowerUp() 里拉高。屏的模拟电源没开时屏完全不工作。 */
#define LCD_VADD_EN_PIN         37       /* PA37 */

#define LCD_PIXEL_WIDTH         390
#define LCD_PIXEL_HEIGHT        450
#define LCD_COL_OFFSET          0
#define LCD_ROW_OFFSET          0

/* 屏寄存器 */
#define CO5300_REG_CASET        0x2A
#define CO5300_REG_RASET        0x2B
#define CO5300_REG_WRITE_RAM    0x2C
#define CO5300_REG_WBRIGHT      0x51

#define CO5300_CMD_CMDLESS      0x02u    /* LCD_WriteReg 里的命令编码前缀 */
#define CO5300_CMD_WRITE_RAM    0x32u    /* 写 GRAM 数据的编码前缀 */

/* ---------------------------------------------------------------------------
 * 简单延时 (厂商代码用 rt_thread_delay / HAL_Delay_us)
 *
 * ★ 旧实现是"空转标定": volatile n = us*40; while (n--); —— 那个 40
 *   ("每 1us 空转 40 圈") 按 240MHz + 每圈 3~4 周期估的, 从未标定。
 *   实测偏慢 80~180 倍: hw_mdelay(10) 约 825ms, hw_udelay(500) 约 89ms,
 *   于是 co5300_init_sequence() 里 260ms 的复位/唤醒时序被放大到 21s。
 *   根因有两个, 且都不是"再标一个常数"能解决的:
 *     1) 频率假设错: 本机 HCPU 真实主频是 144MHz(厂家
 *        HAL_RCC_HCPU_EnableDLL1(144000000)), 不是 240MHz;
 *     2) 更主要的是"每圈几周期"根本不是常数: 该循环带 volatile
 *        (每圈必访存), 又从外部 NOR flash 取指, 真实代价远大于 3~4 周期,
 *        且随取指/cache/中断条件变化 —— 两个调用点实测倍数都不一致
 *        (400000 圈与 20000 圈本应正好差 20 倍, 实测只差约 9 倍)。
 * ★ 因此改成数 DWT->CYCCNT 周期(硬件计数, 与主频/取指无关):
 *   主频用 144000000, 这也与厂家自己的约定一致 ——
 *   bf0_hal_lcdc.c:3290  ptc_delay_1us = HAL_RCC_GetHCLKFreq()/1000000。
 * ------------------------------------------------------------------------- */

#define CYCCNT_FREQ    144000000UL                 /* = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU) */
#define CYCCNT_PER_MS  (CYCCNT_FREQ / 1000UL)      /* 144000 cycles / ms */
#define CYCCNT_PER_US  (CYCCNT_FREQ / 1000000UL)   /* 144 cycles / us */

static int s_dwt_ready = 0;

/* DWT 使能: 幂等, 只在首次真正配置 (重复写 CYCCNT=0 会破坏别的计数者) */
static void dwt_enable(void)
{
  if (s_dwt_ready)
    {
      return;
    }

  /* ARMv8-M 的 DWT 需先解锁: 向 DWT->LAR(0xE0001FB0) 写 0xC5ACCE55 */
  *(volatile uint32_t *)0xE0001FB0UL = 0xC5ACCE55UL;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* TRCENA = 1 */
  DWT->CYCCNT       = 0;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;       /* CYCCNTENA = 1 */
  s_dwt_ready       = 1;
}

/* 忙等 cycles 个 CPU 周期。
 * 32 位回绕用无符号减法 (now - start) 天然处理(模 2^32), 前提是单次
 * 等待 < 2^32 周期(144MHz 下约 29.8s); 本文件最长一次只有 120ms。 */
static void hw_delay_cycles(uint32_t cycles)
{
  uint32_t start;
  uint32_t now;

  if (cycles == 0)
    {
      return;
    }

  dwt_enable();
  start = DWT->CYCCNT;
  do
    {
      now = DWT->CYCCNT;
    }
  while ((uint32_t)(now - start) < cycles);
}

static void hw_udelay(uint32_t us)
{
  uint64_t c = (uint64_t)us * (uint64_t)CYCCNT_PER_US;

  while (c > 0xFFFFFFFFULL)
    {
      hw_delay_cycles(0xFFFFFFFFUL);
      c -= 0xFFFFFFFFULL;
    }

  hw_delay_cycles((uint32_t)c);
}

static void hw_mdelay(uint32_t ms)
{
  uint64_t c = (uint64_t)ms * (uint64_t)CYCCNT_PER_MS;

  while (c > 0xFFFFFFFFULL)
    {
      hw_delay_cycles(0xFFFFFFFFUL);
      c -= 0xFFFFFFFFULL;
    }

  hw_delay_cycles((uint32_t)c);
}

/* ---------------------------------------------------------------------------
 * 进度打印钩子 (由 NuttX 侧/探针注册, 便于定位卡在哪一步)
 * ------------------------------------------------------------------------- */

static void (*s_trace)(const char *msg) = 0;

void sf32lb52_lcd_hw_set_trace(void (*fn)(const char *msg))
{
  s_trace = fn;
}

static void hw_trace(const char *msg)
{
  if (s_trace)
    {
      s_trace(msg);
    }
}

/* ---------------------------------------------------------------------------
 * GPIO: 屏复位 (PA00) 与背光 (PA42)
 *
 * 【寄存器事实】厂家 bsp_power.c:
 *      GPIO_TypeDef *gpio = (is_porta) ? hwp_gpio1 : hwp_gpio2;
 * 且 xty-ai 板 LCD_BL_PIN = 42 且 is_porta=1 ⇒ GPIO1 覆盖 PA00~PA63, 分两组:
 *      第 0 组 PA00~PA31:  DIR0 0x00 / DOR0 0x04 / DOSR0 0x08 / DOCR0 0x0C / DOER0 0x10
 *      第 1 组 PA32~PA63:  DIR1 0x80 / DOR1 0x84 / ...                        DOER1 0x90
 *
 * 之前这里写的是 HPSYS_CFG 的 0x50/0x54 —— 那是 I2C3_PINR/I2C4_PINR,
 * 所以屏复位和背光实际什么都没做 (踩过的坑)。
 * ------------------------------------------------------------------------- */

#define HW_GPIO1_BASE           0x500a0000ul

/* 组内寄存器偏移 (g = 0: PA0x ~ PA31, g = 1: PA32 ~ PA63) */
#define HW_GPIO_REG_DIR(g)      (HW_GPIO1_BASE + ((g) ? 0x80u : 0x00u))
#define HW_GPIO_REG_DOR(g)      (HW_GPIO1_BASE + ((g) ? 0x84u : 0x04u))
#define HW_GPIO_REG_DOER(g)     (HW_GPIO1_BASE + ((g) ? 0x90u : 0x10u))

static void hw_gpio_set(uint32_t pin, uint32_t level)
{
  uint32_t g   = (pin < 32) ? 0 : 1;
  uint32_t bit = pin & 31u;

  volatile uint32_t *dir  = (volatile uint32_t *)HW_GPIO_REG_DIR(g);
  volatile uint32_t *dor  = (volatile uint32_t *)HW_GPIO_REG_DOR(g);
  volatile uint32_t *doer = (volatile uint32_t *)HW_GPIO_REG_DOER(g);

  *dir  &= ~(1u << bit);                 /* 方向: 0 = 输出 (与厂家 GPIO_MODE_OUTPUT 一致) */
  *doer |= (1u << bit);                  /* 输出使能 */
  if (level)
    {
      *dor |= (1u << bit);
    }
  else
    {
      *dor &= ~(1u << bit);
    }
}

/* ---------------------------------------------------------------------------
 * PINMUX
 *
 * 复用我们 M1 里已经验证过的算法 (与厂家 bf0_hal_pinmux.c 一致):
 *   PINR 写 pad - PAD_PA00
 *   引脚 mux 寄存器地址 = PINMUX1_BASE + (pad - 1) * 4
 *   FSEL = (pad - PAD_PA00) + pad 功能号
 * ------------------------------------------------------------------------- */

#define HW_PINMUX1_BASE         0x50003000ul   /* sdk register.h: PINMUX1_BASE */
#define HW_PAD_PA00             1
#define HW_GPIO_FUNC_A0         0        /* 普通 GPIO 的功能号 (枚举首项, 实测 FSEL=4 对应 PA18_I2C_UART) */

static void hw_pinmux(uint32_t pad, uint32_t func)
{
  volatile uint32_t *r =
      (volatile uint32_t *)(HW_PINMUX1_BASE + (pad - 1) * 4);

  /* 每引脚寄存器格式 (sdk cmsis/sf32lb52x/hpsys_pinmux.h):
   *   [3:0] FSEL  功能选择      <- 直接就是功能枚举值本身
   *   [4]   PE    上拉使能
   *   [5]   PS    上拉选择
   *   [6]   IE    输入使能
   *   [7]   IS    输入施密特
   * 注意: 不要用 "pad - pad_base + func" 那个公式 —— 那是给"按外设"的
   *       PINR 寄存器(如 USART1_PINR)用的, 不是按引脚的 FSEL。
   */

  *r = (*r & ~0xFu) | (func & 0xFu);
}

/* ---------------------------------------------------------------------------
 * LCDC 句柄与配置
 * ------------------------------------------------------------------------- */

static LCDC_HandleTypeDef s_hlcdc;

static LCDC_InitTypeDef s_lcdc_cfg =
{
  .lcd_itf    = LCDC_INTF_SPI_DCX_1DATA,     /* 本板: 1 数据线 + DCX */
  .freq       = 48000000,
  .color_mode = LCDC_PIXEL_FORMAT_RGB565,
  .cfg = {
    .spi = {
      .dummy_clock    = 0,
      .syn_mode       = HAL_LCDC_SYNC_DISABLE,   /* 不用 TE 同步 */
      .vsyn_polarity  = 1,
      .vsyn_delay_us  = 0,
      .hsyn_num       = 0,
    },
  },
};

/* ---------------------------------------------------------------------------
 * 屏寄存器读写 (与厂家 co5300.c 的 LCD_WriteReg 等价)
 * ------------------------------------------------------------------------- */

static void co5300_write_reg(uint16_t reg, const uint8_t *params,
                             uint32_t nparams)
{
  uint32_t cmd;

  if (reg == CO5300_REG_WRITE_RAM)
    {
      cmd = ((uint32_t)CO5300_CMD_WRITE_RAM << 24) | ((uint32_t)reg << 8);
    }
  else
    {
      cmd = ((uint32_t)CO5300_CMD_CMDLESS << 24) | ((uint32_t)reg << 8);
    }

  (void)HAL_LCDC_WriteU32Reg(&s_hlcdc, cmd, (uint8_t *)params, nparams);
}

/* 设置刷新窗口 (同时把 LCDC 的 ROI 一起设好, 与厂家一致) */
static void co5300_set_region(uint16_t x0, uint16_t y0, uint16_t x1,
                              uint16_t y1)
{
  uint8_t p[4];
  uint16_t v;

  (void)HAL_LCDC_SetROIArea(&s_hlcdc, x0, y0, x1, y1);

  v = (uint16_t)(x0 + LCD_COL_OFFSET);
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
  v = (uint16_t)(x1 + LCD_COL_OFFSET);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xFF);
  co5300_write_reg(CO5300_REG_CASET, p, 4);

  v = (uint16_t)(y0 + LCD_ROW_OFFSET);
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
  v = (uint16_t)(y1 + LCD_ROW_OFFSET);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xFF);
  co5300_write_reg(CO5300_REG_RASET, p, 4);
}

/* ---------------------------------------------------------------------------
 * CO5300 初始化序列 (逐条对应厂家 co5300.c 的 LCD_Drv_Init)
 * ------------------------------------------------------------------------- */

static void co5300_init_sequence(void)
{
  uint8_t p[4];

  /* 屏复位时序: 高 10ms -> 低 10ms -> 高 50ms */
  hw_gpio_set(LCD_RESET_PIN, 1);
  hw_mdelay(10);
  hw_gpio_set(LCD_RESET_PIN, 0);
  hw_mdelay(10);
  hw_gpio_set(LCD_RESET_PIN, 1);
  hw_mdelay(50);

  /* 密码解锁 */
  p[0] = 0x20;
  co5300_write_reg(0xFE, p, 1);
  p[0] = 0x5A;
  co5300_write_reg(0xF4, p, 1);
  p[0] = 0x59;
  co5300_write_reg(0xF5, p, 1);

  /* 密码锁 */
  p[0] = 0x20;
  co5300_write_reg(0xFE, p, 1);
  p[0] = 0xA5;
  co5300_write_reg(0xF4, p, 1);
  p[0] = 0xA5;
  co5300_write_reg(0xF5, p, 1);

  p[0] = 0x00;
  co5300_write_reg(0xFE, p, 1);
  p[0] = 0x80;
  co5300_write_reg(0xC4, p, 1);

  p[0] = 0x55;                            /* 0x3A: 16bpp RGB565 */
  co5300_write_reg(0x3A, p, 1);

  p[0] = 0x00;
  co5300_write_reg(0x35, p, 1);
  p[0] = 0x20;
  co5300_write_reg(0x53, p, 1);
  p[0] = 0xFF;
  co5300_write_reg(0x63, p, 1);

  /* 列/行窗口 */
  p[0] = (uint8_t)((LCD_COL_OFFSET >> 8) & 0xFF);
  p[1] = (uint8_t)(LCD_COL_OFFSET & 0xFF);
  p[2] = (uint8_t)(((LCD_PIXEL_WIDTH + LCD_COL_OFFSET - 1) >> 8) & 0xFF);
  p[3] = (uint8_t)((LCD_PIXEL_WIDTH + LCD_COL_OFFSET - 1) & 0xFF);
  co5300_write_reg(CO5300_REG_CASET, p, 4);

  p[0] = (uint8_t)((LCD_ROW_OFFSET >> 8) & 0xFF);
  p[1] = (uint8_t)(LCD_ROW_OFFSET & 0xFF);
  p[2] = (uint8_t)(((LCD_PIXEL_HEIGHT + LCD_ROW_OFFSET - 1) >> 8) & 0xFF);
  p[3] = (uint8_t)((LCD_PIXEL_HEIGHT + LCD_ROW_OFFSET - 1) & 0xFF);
  co5300_write_reg(CO5300_REG_RASET, p, 4);

  /* 唤醒 + 开显示 */
  co5300_write_reg(0x11, (const uint8_t *)0, 0);
  hw_mdelay(120);
  co5300_write_reg(0x29, (const uint8_t *)0, 0);
  hw_mdelay(70);
}

/* ---------------------------------------------------------------------------
 * 对外接口 (供 NuttX 侧调用)
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * GPIO 寄存器回读 (诊断用): 确认背光/复位那两个引脚的写是否真的生效
 * ------------------------------------------------------------------------- */

static void hw_hex32(char *p, uint32_t v)
{
  int i;

  for (i = 7; i >= 0; i--)
    {
      uint32_t d = (v >> (i * 4)) & 0xFu;

      *p++ = (char)((d < 10) ? ('0' + d) : ('A' + d - 10));
    }
}

static void hw_dump_gpio(const char *tag)
{
  uint32_t vals[6];
  char     buf[80];
  int      k = 0;
  int      i;
  const char *s = tag;

  while (*s)
    {
      buf[k++] = *s++;
    }

  vals[0] = *(volatile uint32_t *)HW_GPIO_REG_DIR(0);
  vals[1] = *(volatile uint32_t *)HW_GPIO_REG_DOR(0);
  vals[2] = *(volatile uint32_t *)HW_GPIO_REG_DOER(0);
  vals[3] = *(volatile uint32_t *)HW_GPIO_REG_DIR(1);
  vals[4] = *(volatile uint32_t *)HW_GPIO_REG_DOR(1);
  vals[5] = *(volatile uint32_t *)HW_GPIO_REG_DOER(1);

  for (i = 0; i < 6; i++)
    {
      buf[k++] = ' ';
      hw_hex32(&buf[k], vals[i]);
      k += 8;
    }

  buf[k++] = '\n';
  buf[k]   = '\0';
  hw_trace(buf);
}

int sf32lb52_lcd_hw_init(void)
{
  hw_trace("T1-enter\n");

  /* 1. 引脚 mux:
   *      PA00 复位 (普通 GPIO) / PA42 背光 (普通 GPIO)
   *      PA03 CS / PA04 CLK / PA05 DIO0 / PA06 DIO1 (LCDC1 SPI)
   *    功能号直接使用厂家 bsp_pinmux.c 里的同名常量 (由 bf0_hal.h 头文件链提供):
   *      厂家原文: HAL_PIN_Set(PAD_PA03, LCDC1_SPI_CS, PIN_NOPULL, 1);
   *                HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_PULLDOWN, 1);
   */
  hw_pinmux(HW_PAD_PA00 + 0,  GPIO_A0);           /* PA00 -> GPIO */
  hw_pinmux(HW_PAD_PA00 + 42, GPIO_A42);          /* PA42 -> GPIO */
  hw_pinmux(HW_PAD_PA00 + 37, GPIO_A37);          /* PA37 -> GPIO (屏电源使能) */
  hw_pinmux(HW_PAD_PA00 + 3,  LCDC1_SPI_CS);
  hw_pinmux(HW_PAD_PA00 + 4,  LCDC1_SPI_CLK);
  hw_pinmux(HW_PAD_PA00 + 5,  LCDC1_SPI_DIO0);
  hw_pinmux(HW_PAD_PA00 + 6,  LCDC1_SPI_DIO1);
  /* ★【修复】厂家用的是 LCDC_INTF_SPI_DCX_4DATA —— 4 根数据线,
   * 之前只 mux 了 DIO0/DIO1, 漏了 DIO2/DIO3, 4 线接口不完整,
   * 屏收不到通信 => 读 ID 失败 => panel init FAILED。 */
  hw_pinmux(HW_PAD_PA00 + 7,  LCDC1_SPI_DIO2);
  hw_pinmux(HW_PAD_PA00 + 8,  LCDC1_SPI_DIO3);
  hw_trace("T2-pinmux-done\n");

  /* 2. 屏上电: 背光先关, 复位释放, 等 500us (厂家 BSP_LCD_PowerUp 顺序) */
  hw_gpio_set(LCD_BL_PIN, 0);
  /* 按厂家 BSP 顺序上电: 先断电 -> 延时 500us -> 打开屏电源 VADD_EN */
  hw_gpio_set(LCD_VADD_EN_PIN, 0);
  hw_gpio_set(LCD_RESET_PIN, 0);
  hw_udelay(500);
  hw_gpio_set(LCD_VADD_EN_PIN, 1);        /* ★ 屏电源开 */
  hw_gpio_set(LCD_RESET_PIN, 1);
  hw_udelay(500);
  hw_trace("T3-gpio-done ");
  hw_dump_gpio("G1-DIR0/DOR0/DOER0/DIR1/DOR1/DOER1:");

  /* 3. LCDC 初始化 (内含 HAL_RCC_EnableModule -> 给 LCDC1/PINMUX1 开时钟)
   *
   * 【必须先填 Instance】厂商 LCDC_HW_Init() 第一条就是
   *     lcdc->Instance->SETTING |= LCD_IF_SETTING_AUTO_GATE_EN;
   * 若 Instance 为 NULL 就会往地址 0 写, 直接把芯片写挂 (踩过的坑)。
   * LCDC1_BASE = 0x50008000 (sdk register.h)。
   */
  hw_trace("T4-before-lcdc-init\n");
  s_hlcdc.Instance = hwp_lcdc1;

  /* ★【关键修复】必须像厂家 co5300.c 那样先把 Init 配置填好再调 HAL_LCDC_Init:
   *   厂家: memcpy(&hlcdc->Init, &lcdc_int_cfg, sizeof(LCDC_InitTypeDef));
   *   参数(lcdc_int_cfg_qadspi, 在这块板上验证过能亮):
   *     lcd_itf    = LCDC_INTF_SPI_DCX_4DATA   (QAD-SPI, 4 数据线 + DCX)
   *     freq       = 48000000
   *     color_mode = LCDC_PIXEL_FORMAT_RGB565
   *     spi.dummy_clock = 0, syn_mode = HAL_LCDC_SYNC_DISABLE,
   *     spi.vsyn_polarity = 1, vsyn_delay_us = 0, hsyn_num = 0
   *   之前漏了这一步 => lcd_itf=0 / freq=0 => HAL_LCDC_Init 内部按零值配置
   *   (分频/接口选择) => 实测卡死在初始化里。
   */
  s_hlcdc.Init.lcd_itf    = LCDC_INTF_SPI_DCX_4DATA;
  s_hlcdc.Init.freq       = 48000000;
  s_hlcdc.Init.color_mode = LCDC_PIXEL_FORMAT_RGB565;
  s_hlcdc.Init.cfg.spi.dummy_clock   = 0;
  s_hlcdc.Init.cfg.spi.syn_mode      = HAL_LCDC_SYNC_DISABLE;
  s_hlcdc.Init.cfg.spi.vsyn_polarity = 1;
  s_hlcdc.Init.cfg.spi.vsyn_delay_us = 0;
  s_hlcdc.Init.cfg.spi.hsyn_num      = 0;
  /* ★【根因】这里原本还有一行:
   *       s_hlcdc.Init = s_lcdc_cfg;
   *   s_lcdc_cfg 是一个没有赋有效值的结构体(全零), 它把上面刚填好的 Init
   *   整个覆盖成 0 => lcd_itf=0 / freq=0 => HAL_LCDC_Init 内部按零值去配
   *   分频(除零/等锁)与接口选择 => 实测卡死在初始化里(T4->T5 之间)。
   *   厂家做法是把配置 memcpy 进 Init 后直接调 HAL_LCDC_Init, 没有这层覆盖。
   */
  if (HAL_LCDC_Init(&s_hlcdc) != HAL_OK)
    {
      hw_trace("T4b-lcdc-init-FAILED\n");
      return -1;
    }
  hw_trace("T5-lcdc-init-ok\n");

  /* 4. CO5300 初始化序列 */
  hw_trace("T6-before-panel-seq\n");
  co5300_init_sequence();
  hw_trace("T7-panel-seq-done\n");

  /* 5. 默认亮度 50% */
  {
    uint8_t br = (uint8_t)(255 * 50 / 100);
    co5300_write_reg(CO5300_REG_WBRIGHT, &br, 1);
  }

  /* 6. 打开背光 */
  hw_gpio_set(LCD_BL_PIN, 1);
  hw_trace("T8-bl-on ");
  hw_dump_gpio("G2-DIR0/DOR0/DOER0/DIR1/DOR1/DOER1:");

  return 0;
}

void sf32lb52_lcd_hw_set_brightness(uint8_t percent)
{
  uint8_t br;

  if (percent > 100)
    {
      percent = 100;
    }

  br = (uint8_t)((uint32_t)255 * percent / 100);
  co5300_write_reg(CO5300_REG_WBRIGHT, &br, 1);
}

int sf32lb52_lcd_hw_fill(uint16_t rgb565, uint16_t x0, uint16_t y0,
                         uint16_t x1, uint16_t y1)
{
  /* 与厂家 LCD_Clear/LCD_WriteMultiplePixels 一致:
   *   LayerSetData 指定矩形与像素缓冲, 再用 SendLayerData2Reg_IT 把数据
   *   通过 LCDC 推到屏的 GRAM。
   *
   * 【注意】这里需要一块像素缓冲。为验证"能否出图", 先用静态缓冲:
   *   一次最多刷 LCD_TEST_BLOCK 个像素, 循环覆盖整个区域。
   *   后续做 /dev/fb0 时会把整屏 framebuffer 交给 LayerSetData。
   */

  static uint16_t s_line[64];
  uint32_t i;
  uint16_t yy;

  for (i = 0; i < 64; i++)
    {
      s_line[i] = rgb565;
    }

  for (yy = y0; yy <= y1; yy += 1)
    {
      uint16_t xx;

      for (xx = x0; xx <= x1; xx += 64)
      {
        uint16_t w = (uint16_t)((x1 - xx + 1) > 64 ? 64 : (x1 - xx + 1));

        co5300_set_region(xx, yy, (uint16_t)(xx + w - 1), yy);
        (void)HAL_LCDC_LayerSetData(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT,
                                    (uint8_t *)s_line, xx, yy,
                                    (uint16_t)(xx + w - 1), yy);
        (void)HAL_LCDC_SendLayerData2Reg_IT(&s_hlcdc,
                                            ((uint32_t)CO5300_CMD_WRITE_RAM << 24)
                                            | ((uint32_t)CO5300_REG_WRITE_RAM << 8),
                                            4);
      }
    }

  return 0;
}

void sf32lb52_lcd_hw_test(void)
{
  /* 红 -> 绿 -> 蓝, 各停留一小段时间 */
  sf32lb52_lcd_hw_fill(0xF800, 0, 0, LCD_PIXEL_WIDTH - 1, LCD_PIXEL_HEIGHT - 1);
  hw_mdelay(500);
  sf32lb52_lcd_hw_fill(0x07E0, 0, 0, LCD_PIXEL_WIDTH - 1, LCD_PIXEL_HEIGHT - 1);
  hw_mdelay(500);
  sf32lb52_lcd_hw_fill(0x001F, 0, 0, LCD_PIXEL_WIDTH - 1, LCD_PIXEL_HEIGHT - 1);
}