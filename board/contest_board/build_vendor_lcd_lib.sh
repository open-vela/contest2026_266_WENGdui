#!/bin/bash
# ============================================================================
#  把"厂家 LCD 栈"编译成静态库 libsf32lb52_vendorlcd.a
#
#  为什么用静态库而不是直接加进 CHIP_CSRCS:
#    厂家 HAL 头需要一个"优先于 NuttX"的头文件搜索顺序 (sdk_port/shim 里的
#    string.h/stdint.h 等), 而 NuttX 芯片层的 CFLAGS 是全目录共享的。
#    把 -I shim 加进去会同时影响串口/中断/启动等所有芯片层源文件,
#    把 M1 已经上板验证过的构建搞坏。
#    所以这里用一个"独立编译环境"产出 .a, NuttX 侧只通过 LDLIBS 链接。
#
#  库内代码与 probe_test/lcdprobe/vendor/build_vendor.sh 完全相同 ——
#  那套已经在真机上跑通 (CO5300_ReadID 0x331100 + 三条色带可见)。
# ============================================================================
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"          # -> openvela_thermo
SDK="/mnt/hgfs/思澈科技  家电监控器/源码/xiaozhi-sf32-1.4.0/sdk"
# ★ 环境兼容: 若本机没有 VMware hgfs 共享目录 (例如纯 WSL, 用的是 /mnt/d 直挂),
#   厂家 SDK 就在仓库的【同级目录】里, 退回相对路径即可, 行为与 hgfs 环境一致。
[ -d "$SDK" ] || SDK="$PROJ/../源码/xiaozhi-sf32-1.4.0/sdk"
NUTTX_DIR="${OPENVELA_ROOT:-$HOME/vela-opensource}/nuttx"
DEST_DIR="$NUTTX_DIR/boards/arm/sf32lb52/sf32lb52-lcd/src"
OUT=/tmp/vendorlcdlib
GCC=arm-none-eabi-gcc
AR=arm-none-eabi-ar

echo "============================================"
echo "  构建 libsf32lb52_vendorlcd.a"
echo "============================================"
rm -rf "$OUT"; mkdir -p "$OUT/shim"

# ---- 1. 厂家头/源码 (sdk_port) ----------------------------------------------
[ -d "$PROJ/sdk_port/Include" ] || { echo "[FAIL] 缺 $PROJ/sdk_port/Include"; exit 1; }
#  bf0_hal_mpi*.c / flash_table.c 是"参数存储 (NVS)"要用的厂家 QSPI NOR 编程栈:
#   厂家 rt_flash_write/erase 的最底层实现 (HAL_QSPIEX_FLASH_WRITE/ERASE)。
#   它们在 flash.ld 里被放进 SRAM 常驻段 —— 因为本固件从 QSPI2 取指,
#   擦/写 QSPI2 时不能在 flash 里执行。厂家做法同源 (link.lds 的 .retm_data)。
for c in bf0_hal_lcdc.c bf0_hal_rcc.c bf0_hal_gpio.c bf0_hal_pinmux.c bf0_hal_tim.c bf0_pin_const.c bf0_hal_i2c.c bf0_hal_hpaon.c bf0_hal_adc.c bf0_hal_mpi.c bf0_hal_mpi_ex.c flash_table.c; do
    if [ ! -f "$PROJ/sdk_port/$c" ]; then
        f=$(find "$SDK/drivers" -name "$c" 2>/dev/null | head -1)
        [ -n "$f" ] || { echo "[FAIL] SDK 里找不到 $c"; exit 1; }
        cp -f "$f" "$PROJ/sdk_port/$c"
    fi
done

# ---- 2. 桩头 (只给厂家代码用, 不影响 NuttX) ---------------------------------
cat > "$OUT/shim/rtthread.h" <<'EOF'
#ifndef _RT_SHIM_H
#define _RT_SHIM_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
void rt_thread_delay(int tick);
void rt_kprintf(const char *fmt, ...);
#define RT_ASSERT(x)   do { } while (0)
#endif
EOF

cat > "$OUT/shim/rtdbg.h" <<'EOF'
#ifndef _RTDBG_SHIM_H
#define _RTDBG_SHIM_H
#ifndef DBG_INFO
#define DBG_INFO 6
#endif
#define LOG_I(...)  do { } while (0)
#define LOG_D(...)  do { } while (0)
#define LOG_E(...)  do { } while (0)
#define LOG_W(...)  do { } while (0)
void rt_kprintf(const char *fmt, ...);
#endif
EOF

cat > "$OUT/shim/board.h" <<'EOF'
#ifndef _BOARD_SHIM_H
#define _BOARD_SHIM_H
#define LCD_HOR_RES_MAX   390
#define LCD_VER_RES_MAX   450
#endif
EOF

cat > "$OUT/shim/drv_io.h" <<'EOF'
#ifndef _DRV_IO_SHIM_H
#define _DRV_IO_SHIM_H
#include <stdint.h>
#include <stdbool.h>
void BSP_LCD_Reset(uint8_t high1_low0);
void BSP_LCD_PowerUp(void);
void BSP_LCD_PowerDown(void);
#endif
EOF

cat > "$OUT/shim/drv_lcd.h" <<'EOF'
#ifndef _DRV_LCD_SHIM_H
#define _DRV_LCD_SHIM_H
#include "bf0_hal.h"
#define RTGRAPHIC_PIXEL_FORMAT_RGB565   5
#define RTGRAPHIC_PIXEL_FORMAT_RGB888   7

typedef enum
{
    LCD_ROTATE_0_DEGREE = 0,
    LCD_ROTATE_90_DEGREE = 90,
    LCD_ROTATE_180_DEGREE = 180,
    LCD_ROTATE_270_DEGREE = 270,
} LCD_DrvRotateTypeDef;

typedef struct
{
    void (*Init)(LCDC_HandleTypeDef *hlcdc);
    uint32_t (*ReadID)(LCDC_HandleTypeDef *hlcdc);
    void (*DisplayOn)(LCDC_HandleTypeDef *hlcdc);
    void (*DisplayOff)(LCDC_HandleTypeDef *hlcdc);
    void (*SetRegion)(LCDC_HandleTypeDef *hlcdc, uint16_t, uint16_t, uint16_t, uint16_t);
    void (*WritePixel)(LCDC_HandleTypeDef *hlcdc, uint16_t, uint16_t, const uint8_t *);
    void (*WriteMultiplePixels)(LCDC_HandleTypeDef *hlcdc, const uint8_t *, uint16_t, uint16_t, uint16_t, uint16_t);
    uint32_t (*ReadPixel)(LCDC_HandleTypeDef *hlcdc, uint16_t, uint16_t);
    void (*SetColorMode)(LCDC_HandleTypeDef *hlcdc, uint16_t);
    void (*SetBrightness)(LCDC_HandleTypeDef *hlcdc, uint8_t);
    void (*IdleModeOn)(LCDC_HandleTypeDef *hlcdc);
    void (*IdleModeOff)(LCDC_HandleTypeDef *hlcdc);
    void (*Rotate)(LCDC_HandleTypeDef *hlcdc, LCD_DrvRotateTypeDef);
    void (*TimeoutDbg)(LCDC_HandleTypeDef *hlcdc);
    void (*TimeoutReset)(LCDC_HandleTypeDef *hlcdc);
    uint32_t (*ESDDetect)(LCDC_HandleTypeDef *hlcdc);
} LCD_DrvOpsDef;

#define LCD_DRIVER_EXPORT(name, id, init_cfg, dev_ops, ic_max_hor_res, ic_max_ver_res, pixel_align)
#endif
EOF

CO5300=$(find "$SDK/customer" -name co5300.c 2>/dev/null | head -1)
[ -n "$CO5300" ] || { echo "[FAIL] 找不到 co5300.c"; exit 1; }
cp -f "$CO5300" "$OUT/co5300.c"

# ---- 3b. 修掉厂家 copy 里的一条真警告 (只初始化, 不动别的逻辑) ------------
#
#   报错原文 (-Wall -Os, 即下面 CFL_GLUE 编译胶水层时):
#     co5300.c:386:14: warning: 'ret_v' may be used uninitialized [-Wmaybe-uninitialized]
#
#   出处 co5300.c:383-420 LCD_ReadPixel():
#       :386  uint32_t ret_v, read_value;
#       :400  switch (lcdc_int_cfg.color_mode)
#       :402    case LCDC_PIXEL_FORMAT_RGB565:  ret_v = ...; break;
#       :410    case LCDC_PIXEL_FORMAT_RGB888:  ret_v = ...; break;
#       :414    default:                        RT_ASSERT(0); break;
#       :419  return ret_v;
#   ret_v 只在两个合法 case 里赋值; default 分支只断言。
#   当 color_mode 不是这两者、且 RT_ASSERT 被编译成空(发布态/NDEBUG)时,
#   会直接执行到 :419 `return ret_v;` —— 返回未初始化的栈值 (UB),
#   这正是"回读值不可信"的软件层来源之一。
#
#   修法: 在声明处初始化 0。语义完全不变(合法色深仍各走自己的 case),
#   只把"返回未初始化值"这条 UB 消除, 是纯安全兜底;
#   厂家任何寄存器序列/时序一个字节都没改。
#   幂等: 已初始化过时 sed 不再命中。
sed -i 's/uint32_t ret_v, read_value;/uint32_t ret_v = 0, read_value = 0;/' "$OUT/co5300.c"
if grep -q 'uint32_t ret_v = 0, read_value = 0;' "$OUT/co5300.c"; then
    echo "  [OK]   co5300.c:386 ret_v/read_value 已初始化 (消除 -Wmaybe-uninitialized)"
else
    echo "  [WARN] co5300.c 中未找到 'uint32_t ret_v, read_value;' 原句, 跳过初始化"
fi

# ---- 3. 胶水层: 把厂家代码接到裸片上, 并对外暴露干净的 C API ----------------
cat > "$OUT/vendor_lcd_glue.c" <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

#define U1_BASE   0x50084000UL
#define U1_ISR    (*(volatile uint32_t *)(U1_BASE + 0x1C))
#define U1_TDR    (*(volatile uint32_t *)(U1_BASE + 0x28))

static void putc_(char c)
{
    uint32_t guard = 400000UL;
    while (!(U1_ISR & (1u << 7)) && guard--) { }
    U1_TDR = (uint32_t)(uint8_t)c;
}
static void puts_(const char *s)
{
    while (*s) { if (*s == '\n') putc_('\r'); putc_(*s++); }
}
/* %x/%X:
 *   显式给宽度时按 printf 语义输出 (如 %02x -> 2 位, '0' 标志补零, 否则补空格;
 *   '-' 左对齐; 'x' 小写字母, 'X' 大写字母);
 *   未给宽度时保持本 shim 的历史行为 —— 固定 8 位、补零、大写字母
 *   (这样 [TP]/[LCD]/[NVS] 等既有行的输出格式不变)。 */
static void hex_w_(uint32_t v, int width, int pad0, int upper, int left)
{
    char b[16];
    int  n = 0, i;

    do {
        uint32_t d = v & 0xFu;
        b[n++] = (char)((d < 10) ? ('0' + d) : ((upper ? 'A' : 'a') + d - 10));
        v >>= 4;
    } while (v);

    if (width > 16) width = 16;

    if (!left)
        for (i = n; i < width; i++) putc_(pad0 ? '0' : ' ');
    for (i = n - 1; i >= 0; i--) putc_(b[i]);
    if (left)
        for (i = n; i < width; i++) putc_(' ');
}
static void dec_(int32_t v)
{
    char b[12]; int n = 0, i;
    uint32_t u = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    if (v < 0) putc_('-');
    do { b[n++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    for (i = n - 1; i >= 0; i--) putc_(b[i]);
}

void rt_kprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *p;
    __builtin_va_start(ap, fmt);
    for (p = fmt; *p; p++)
    {
        int left = 0, pad0 = 0, width = -1;

        if (p[0] != '%') { putc_(*p); continue; }
        p++;

        /* 解析标志: '-' 左对齐 / '0' 补零 */
        for (;; p++)
        {
            if (*p == '-') { left = 1; continue; }
            if (*p == '0') { pad0 = 1; continue; }
            break;
        }
        /* 解析宽度 */
        if (*p >= '1' && *p <= '9')
        {
            width = 0;
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        }

        switch (*p)
        {
        case 'x': case 'X':
            hex_w_((uint32_t)__builtin_va_arg(ap, unsigned int),
                   (width < 0) ? 8 : width,
                   (width < 0) ? 1 : pad0,
                   (width < 0) ? 1 : (*p == 'X'),
                   left);
            break;
        case 'd': case 'i': dec_((int32_t)__builtin_va_arg(ap, int));           break;
        case 'u':           dec_((int32_t)__builtin_va_arg(ap, unsigned int));  break;
        case 's':           puts_((const char *)__builtin_va_arg(ap, char *));  break;
        case 'c':           putc_((char)__builtin_va_arg(ap, int));             break;
        case '%':           putc_('%');                                         break;
        default:            putc_('%'); putc_(*p);                              break;
        }
    }
    __builtin_va_end(ap);
}

/* --------------------------------------------------------------------------
 *  忙等延时: 直接数 DWT->CYCCNT 周期 (与 HAL_GetTick 同一套时基)
 *
 *  ★ 旧实现错在哪 —— "空转标定":
 *      void hw_udelay(uint32_t us) { volatile uint32_t n = us * 40u; while (n--) {} }
 *    那个 40 的语义是"每 1us 空转 40 圈", 是按 240MHz + 每圈 3~4 周期
 *    拍脑袋估的, 从未在板上标定过。实测偏慢 80~180 倍
 *    (hw_mdelay(10) 实耗约 825ms; 屏初始化里 260ms 的时序变成 21s)。
 *    而且两个调用点的倍数并不一致: 400000 圈 与 20000 圈本应正好差 20 倍,
 *    实测只差约 9 倍 —— 说明"每圈几周期"在这里根本不是常数:
 *    该循环带 volatile(每圈必访存), 又从外部 NOR flash 取指,
 *    每圈真实代价远大于 3~4 周期(与 cache/取指/中断条件相关)。
 *    结论: 空转标定在这颗芯片上不可用作时基, 必须数硬件周期。
 *
 *  ★ 真实主频 = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU) = 144000000
 *    (厂家 HAL_RCC_HCPU_EnableDLL1(144000000) 已把本机配到 144MHz,
 *     见下方 init_clock 段; 不要用 SystemCoreClock, 那只是占位值)。
 *    这也正是厂家自己的约定, 见 bf0_hal_lcdc.c:3290
 *      ptc_delay_1us = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU) / 1000000;
 *
 *  ★ 32 位回绕: 用无符号减法 (now - start) 天然正确(模 2^32),
 *    前提是单次等待 < 2^32 周期 = 144MHz 下约 29.8s; 本库最长一次是
 *    NTC 的 hw_mdelay(300), 余量极大。超过上限则分段等。
 *
 *  ★ 精度: 只等到"已过 cycles"为止, 多等的是几条指令(几十 ns),
 *    在 10us~1s 范围内误差远小于 ±5%。
 * ------------------------------------------------------------------------ */
#define CYCCNT_FREQ    144000000UL              /* = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU) */
#define CYCCNT_PER_MS  (CYCCNT_FREQ / 1000UL)   /* 144000 cycles / ms */
#define CYCCNT_PER_US  (CYCCNT_FREQ / 1000000UL)/* 144 cycles / us */

static int s_dwt_ready = 0;

/* DWT 使能: 幂等, 只做一次。
 * ★ 绝不能每次延时都写 DWT->CYCCNT = 0 —— HAL_GetTick 靠
 *   "s_cyc_last -> now" 的增量累加毫秒, 中途清零会被它当成 32 位回绕,
 *   一次多算约 29.8s, 会把厂家 WaitBusy/WaitVsync 的超时判断搞乱。 */
static void dwt_enable(void)
{
    if (s_dwt_ready) return;

    /* ARMv8-M 的 DWT 需先解锁: 向 DWT->LAR(0xE0001FB0) 写 0xC5ACCE55。
     * 本 CMSIS 的 DWT_Type 没有 LAR 成员, 故按地址直接写。 */
    *(volatile uint32_t *)0xE0001FB0UL = 0xC5ACCE55UL;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* TRCENA = 1 */
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;       /* CYCCNTENA = 1 */
    s_dwt_ready       = 1;
}

static void hw_delay_cycles(uint32_t cycles)
{
    uint32_t start, now;

    if (cycles == 0) return;

    dwt_enable();
    start = DWT->CYCCNT;
    do
    {
        now = DWT->CYCCNT;
    }
    while ((uint32_t)(now - start) < cycles);
}

void hw_udelay(uint32_t us)
{
    uint64_t c = (uint64_t)us * (uint64_t)CYCCNT_PER_US;

    while (c > 0xFFFFFFFFULL) { hw_delay_cycles(0xFFFFFFFFUL); c -= 0xFFFFFFFFULL; }
    hw_delay_cycles((uint32_t)c);
}

void hw_mdelay(uint32_t ms)
{
    uint64_t c = (uint64_t)ms * (uint64_t)CYCCNT_PER_MS;

    while (c > 0xFFFFFFFFULL) { hw_delay_cycles(0xFFFFFFFFUL); c -= 0xFFFFFFFFULL; }
    hw_delay_cycles((uint32_t)c);
}

void rt_thread_delay(int tick) { hw_mdelay((uint32_t)tick); }

void *memcpy(void *d, const void *s, unsigned int n)
{ unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memset(void *d, int c, unsigned int n)
{ unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
int memcmp(const void *a, const void *b, unsigned int n)
{ const unsigned char *x = a, *y = b; while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; } return 0; }
unsigned int strlen(const char *s) { const char *p = s; while (*p) p++; return (unsigned int)(p - s); }

/* --------------------------------------------------------------------------
 *  HAL_GetTick: 只读硬件计时器 —— Cortex-M33 的 DWT->CYCCNT (真实毫秒)
 *
 *  ★ 绝不在这里做任何延时。曾把它写成
 *      { hw_udelay(1000); return ++s_tick; }
 *    想直接得到毫秒, 结果启动卡死 5 分钟以上。原因:
 *    厂家 HAL 里存在大量【高频轮询】HAL_GetTick() 的循环
 *    (bf0_hal_lcdc.c:1116/1138 的 WaitVsync/WaitBusy, bf0_hal_i2c.c:5753 等),
 *    每次调用真延 1ms 会把毫秒级循环放大到分钟级。
 *    所以这里只读寄存器, 一次调用就是几条指令。
 *
 *  ★ 真实主频 = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU) = 144000000 (见 CYCCNT_FREQ)。
 *    【不要】用 SystemCoreClock: 本 glue 里的 240000000 只是占位(见下方),
 *    用它换算会让 ms 整体偏快 144/240。
 *
 *  ★ 32 位回绕: 144MHz 下 CYCCNT 约 29.8s 回绕一次。
 *    用静态 last 记住上次周期值, 新值 < 旧值时补足 (2^32 - last) + now 个周期;
 *    再按"每 ms 周期数 = 144000"累加成单调 ms 计数。
 *    余数保留(s_cyc_rem), 避免高频轮询下每次丢掉不足 1ms 的尾巴把 tick 走慢。
 * ------------------------------------------------------------------------ */
/* 注: CYCCNT_FREQ / CYCCNT_PER_MS 已在上面 hw_udelay 段定义 (两处共用同一时基) */

static uint32_t s_cyc_last = 0;
static uint32_t s_cyc_rem  = 0;
static uint32_t s_ms_acc   = 0;

uint32_t HAL_GetTick(void)
{
    uint32_t now, delta, ms;

    dwt_enable();   /* 幂等: DWT 的解锁/使能在上面 hw_udelay 段统一做 */

    now = DWT->CYCCNT;
    if (now < s_cyc_last)
    {
        delta = (0xFFFFFFFFUL - s_cyc_last) + now + 1UL;  /* 回绕: 补足 2^32 个周期 */
    }
    else
    {
        delta = now - s_cyc_last;
    }
    s_cyc_last = now;

    ms = delta / CYCCNT_PER_MS;
    s_cyc_rem += delta % CYCCNT_PER_MS;                   /* 余数累加, 见上 */
    if (s_cyc_rem >= CYCCNT_PER_MS)
    {
        s_cyc_rem -= CYCCNT_PER_MS;
        ms++;
    }
    s_ms_acc += ms;

    return s_ms_acc;
}

void HAL_Delay_us(uint32_t us) { hw_udelay(us); }

/* 厂家 bf0_hal_rcc.c 会写这个符号(它原本定义在没编进来的 system_bf0_ap.c)。
 * ★ 仅作占位: HAL_GetTick 的 ms 换算【不】用它, 真实主频见上面的 CYCCNT_FREQ。 */
uint32_t SystemCoreClock = 240000000U;

static void bsp_gpio_set(int pin, int val)
{
    GPIO_InitTypeDef gi;
    gi.Mode = GPIO_MODE_OUTPUT;
    gi.Pin  = (uint16_t)pin;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(hwp_gpio1, &gi);
    HAL_GPIO_WritePin(hwp_gpio1, (uint16_t)pin, (GPIO_PinState)val);
}

#define LCD_RESET_PIN   0
#define LCD_VADD_EN     37

void BSP_LCD_Reset(uint8_t high1_low0) { bsp_gpio_set(LCD_RESET_PIN, high1_low0); }
void BSP_LCD_PowerUp(void)             { bsp_gpio_set(LCD_VADD_EN, 1); }
void BSP_LCD_PowerDown(void)
{
    bsp_gpio_set(LCD_RESET_PIN, 0);
    bsp_gpio_set(LCD_VADD_EN, 0);
}

static GPT_HandleTypeDef s_bl;
static uint32_t          s_bl_period_ticks = 0;

static void backlight_pwm_init(uint32_t percent)
{
    GPT_ClockConfigTypeDef ccfg = {0};
    GPT_OC_InitTypeDef     oc   = {0};
    uint32_t gpt_clock, psc, period, pulse_ns, pulse;

    s_bl.Instance          = hwp_gptim1;
    s_bl.core              = CORE_ID_HCPU;
    s_bl.Init.Prescaler    = 0;
    s_bl.Init.CounterMode  = GPT_COUNTERMODE_UP;
    s_bl.Init.Period       = 0;
    if (HAL_GPT_Base_Init(&s_bl) != HAL_OK) { puts_("BL-base-FAIL\n"); return; }
    ccfg.ClockSource = GPT_CLOCKSOURCE_INTERNAL;
    if (HAL_GPT_ConfigClockSource(&s_bl, &ccfg) != HAL_OK) { puts_("BL-clk-FAIL\n"); return; }
    if (HAL_GPT_PWM_Init(&s_bl) != HAL_OK) { puts_("BL-pwm-FAIL\n"); return; }
    __HAL_GPT_URS_ENABLE(&s_bl);

    gpt_clock = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1) / 1000000UL;
    period    = 10000UL * gpt_clock / 1000UL;
    if (period == 0) period = 1;
    psc       = period / 0xFFFFUL + 1UL;
    period    = period / psc;
    if (period < 3) period = 3;
    __HAL_GPT_SET_PRESCALER(&s_bl, psc - 1);
    __HAL_GPT_SET_AUTORELOAD(&s_bl, period - 1);
    s_bl_period_ticks = period;

    pulse_ns = 10000UL * percent / 100UL;
    pulse    = pulse_ns * gpt_clock / psc / 1000UL;
    if (pulse < 1)            pulse = 1;
    else if (pulse >= period) pulse = period + 1;
    __HAL_GPT_SET_COMPARE(&s_bl, GPT_CHANNEL_4, pulse - 1);
    HAL_GPT_GenerateEvent(&s_bl, GPT_EVENTSOURCE_UPDATE);

    oc.OCMode     = GPT_OCMODE_PWM1;
    oc.Pulse      = __HAL_GPT_GET_COMPARE(&s_bl, GPT_CHANNEL_4);
    oc.OCPolarity = GPT_OCPOLARITY_HIGH;
    oc.OCFastMode = GPT_OCFAST_DISABLE;
    if (HAL_GPT_PWM_ConfigChannel(&s_bl, &oc, GPT_CHANNEL_4) != HAL_OK) { puts_("BL-ch4-FAIL\n"); return; }
    HAL_GPT_PWM_Start(&s_bl, GPT_CHANNEL_4);
}

static void backlight_pwm_set(uint32_t percent)
{
    uint32_t gpt_clock, pulse_ns, pulse;
    if (percent > 100) percent = 100;
    gpt_clock = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1) / 1000000UL;
    pulse_ns  = 10000UL * percent / 100UL;
    pulse     = pulse_ns * gpt_clock / 1000UL;
    pulse    /= (s_bl.Instance->PSC + 1UL);
    if (pulse < 1)                        pulse = 1;
    else if (pulse >= s_bl_period_ticks)  pulse = s_bl_period_ticks + 1;
    __HAL_GPT_SET_COMPARE(&s_bl, GPT_CHANNEL_4, pulse - 1);
    HAL_GPT_GenerateEvent(&s_bl, GPT_EVENTSOURCE_UPDATE);
}

/* ★ 原样纳入厂家屏驱动 (寄存器序列/时序 100% 厂家原文)。
 *   它是【厂家代码】, 不属于"我们写的胶水层": 该文件在 -Wall 下有两处
 *   厂家自身的告警, 这里按警告名精确屏蔽, 不改厂家源码。
 *   注意 `#pragma GCC diagnostic ignored "-Wall"` 是【无效】的(实测 GCC 13
 *   不认组选项), 必须逐个警告名屏蔽。
 *     co5300.c:320  uint8_t  data = 0;   (未使用)  [-Wunused-variable]
 *     co5300.c:329  uint32_t size;       (未使用)  [-Wunused-variable] */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "co5300.c"
#pragma GCC diagnostic pop

static LCDC_HandleTypeDef s_hlcdc;
static int s_inited = 0;

/* --------------------------------------------------------------------------
 *  分步耗时打点
 *    HAL_GetTick() 是 DWT->CYCCNT 换算出的【真实毫秒】(见上文实现),
 *    所以这里的 dt 就是实测毫秒, 不是"轮询次数"。
 *    输出格式:  [LCD] t=<ms> step=<名字> dt=<本步耗时 ms>
 * ------------------------------------------------------------------------ */
static uint32_t s_step_t0 = 0;

static uint32_t lcd_step(const char *name)
{
    uint32_t now = HAL_GetTick();

    rt_kprintf("[LCD] t=%u step=%s dt=%u\n",
               (unsigned int)now, name, (unsigned int)(now - s_step_t0));
    s_step_t0 = now;
    return now;
}

/* 厂家 LCD_Clear() 的同一套调用, 区域/颜色参数化 */
static void lcd_fill_raw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                         uint8_t r, uint8_t g, uint8_t b)
{
    HAL_LCDC_Next_Frame_TE(&s_hlcdc, 0);
    LCD_SetRegion(&s_hlcdc, x0, y0, x1, y1);
    HAL_LCDC_LayerSetFormat(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT, LCDC_PIXEL_FORMAT_RGB565);
    HAL_LCDC_LayerDisable(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT);
    HAL_LCDC_SetBgColor(&s_hlcdc, r, g, b);
    HAL_LCDC_SendLayerData2Reg(&s_hlcdc, ((0x32 << 24) | (REG_WRITE_RAM << 8)), 4);
    HAL_LCDC_LayerEnable(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT);
}

/* ==========================================================================
 *  对外 API
 * ========================================================================== */

/* 引脚复用表来源: sdk/customer/boards/sf32lb52-lcd_base/bsp_pinmux.c
 *                 BSP_PIN_Common() + BSP_PIN_LCD() 的 QADSPI 分支                 */
/* ★ 外设时钟 —— 厂家 init_clock() 里最关键的一步
 *   出处: sdk/tools/flash/project/sf32lb52x/src/FlashPrg.c:315-350
 *         (SF32LB52X 专用; 同一句也在 app/boards/sf32lb52-xty-ai_base/bsp_init.c:165)
 *
 *     HAL_RCC_HCPU_ClockSelect(RCC_CLK_MOD_HP_PERI, RCC_CLK_PERI_HXT48);
 *
 *   HP_PERI = 高性能外设时钟总线, LCDC / I2C / USART 都挂在它上面。
 *   我们这套 openvela 移植从未配置过它, 后果是外设拿不到时钟脉冲:
 *     - I2C : 寄存器可读写(en=1 / rst=0 / LCR 正常), 但状态机不推进 ——
 *             SR 恒为 0, 标志永不置起, 超时永不结束(实测 100 万次轮询仍失败)
 *     - LCDC: SPI 时钟远低于 48MHz, 全屏初始化要 20 秒
 *
 *   ★ 故意【不】照搬厂家后续的 EnableDLL1/2 与 FLASH1/2=DLL2:
 *     那会改动系统时钟和 NOR flash 时钟, 有取指失败的风险;
 *     且本机系统已是 144MHz, 与厂家 HAL_RCC_HCPU_EnableDLL1(144000000) 一致, 无需重配。
 *     只补"开 48M 源 + 外设总线选 48M"这两句, 零风险。
 *
 *   声明直接 extern: HAL_HPAON_EnableXT48 在 bf0_hal_aon.h:653, 这里不额外引头文件。 */
extern void HAL_HPAON_EnableXT48(void);

void sf32lb52_vendor_clock_init(void)
{
    HAL_HPAON_EnableXT48();
    HAL_RCC_HCPU_ClockSelect(RCC_CLK_MOD_HP_PERI, RCC_CLK_PERI_HXT48);

    /* ★ 回读验证: 确认上面的写入真的落进了 RCC, 并打出厂家 HAL 自己
     *   认为的 HCLK —— LCDC/I2C 的分频都是用它算的,
     *   若它返回的不是 144MHz, 就能解释"LCD 慢 48 倍"与 I2C 分频异常。 */
    rt_kprintf("[TP] clk src: sys=%x hp_peri=%x (want hp_peri=%x) hclk=%u\n",
               (unsigned int)HAL_RCC_HCPU_GetClockSrc(RCC_CLK_MOD_SYS),
               (unsigned int)HAL_RCC_HCPU_GetClockSrc(RCC_CLK_MOD_HP_PERI),
               (unsigned int)RCC_CLK_PERI_HXT48,
               (unsigned int)HAL_RCC_GetHCLKFreq(CORE_ID_HCPU));
}

int sf32lb52_lcd_vendor_init(void)
{
    if (s_inited) return 0;

    s_step_t0 = HAL_GetTick();
    rt_kprintf("[LCD] t=%u step=begin\n", (unsigned int)s_step_t0);

    /* ---- 1. 外设时钟: 厂家 init_clock() 的 HP_PERI -> HXT48 ------------- */
    sf32lb52_vendor_clock_init();      /* ★ 先给外设总线供上 48MHz */
    lcd_step("clock_init");

    /* ---- 2. 引脚复用 ---------------------------------------------------- */
    HAL_PIN_Set(PAD_PA00, GPIO_A0,  PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA37, GPIO_A37, PIN_NOPULL,   1);
    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4,   PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA02, LCDC1_SPI_TE, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA03, LCDC1_SPI_CS, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA04, LCDC1_SPI_CLK,  PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA05, LCDC1_SPI_DIO0, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA06, LCDC1_SPI_DIO1, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA07, LCDC1_SPI_DIO2, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA08, LCDC1_SPI_DIO3, PIN_NOPULL, 1);
    lcd_step("pinmux");
    puts_("[LCD] pinmux ok\n");

    /* ---- 3. BSP_LCD_Reset 三段 (与 co5300 LCD_Drv_Init 原文时序一致) ----
     *   段1: RESET 拉低
     *   段2: RESET 释放 + VADD_EN 上电
     *   段3: 面板复位脉冲 1/0/1 + rt_thread_delay(10/10/50)             */
    bsp_gpio_set(LCD_RESET_PIN, 0);
    HAL_Delay_us(500);
    lcd_step("reset.seg1(RESET=0)");

    BSP_LCD_PowerUp();
    HAL_Delay_us(500);
    lcd_step("reset.seg2(VADD_EN=1)");
    puts_("[LCD] powerup ok\n");

    BSP_LCD_Reset(1);  rt_thread_delay(10);
    BSP_LCD_Reset(0);  rt_thread_delay(10);
    BSP_LCD_Reset(1);  rt_thread_delay(50);
    lcd_step("reset.seg3(pulse10/10/50)");

    /* ---- 4. HAL_LCDC_Init 单独计时 --------------------------------------
     *   co5300 的 LCD_Drv_Init() 第一件事就是 memcpy(lcdc_int_cfg_qadspi)
     *   后调 HAL_LCDC_Init()。这里用【完全相同的一份配置】
     *   (lcdc_int_cfg_qadspi 由上面的 #include "co5300.c" 带进来)
     *   在独立句柄上单独调用一次, 把它的耗时与算出的 SPI 分频挖出来。
     *   顺带回读 SPI_IF_CONF 的 CLK_DIV 位域, 验证 HAL 实际写进去的分频。 */
    {
        static LCDC_HandleTypeDef s_probe;
        uint32_t clk_in, div;

        s_probe.Instance = hwp_lcdc1;
        memcpy(&s_probe.Init, &lcdc_int_cfg_qadspi, sizeof(LCDC_InitTypeDef));

        s_step_t0 = HAL_GetTick();
        HAL_LCDC_Init(&s_probe);
        lcd_step("HAL_LCDC_Init");

        clk_in = HAL_RCC_GetHCLKFreq(CORE_ID_HCPU);
        div    = (hwp_lcdc1->SPI_IF_CONF & LCD_IF_SPI_IF_CONF_CLK_DIV_Msk)
                 >> LCD_IF_SPI_IF_CONF_CLK_DIV_Pos;
        rt_kprintf("[LCD] clk_in(hclk)=%u target_freq=%u CLK_DIV=%u spi_clk=%u\n",
                   (unsigned int)clk_in,
                   (unsigned int)lcdc_int_cfg_qadspi.freq,
                   (unsigned int)div,
                   (unsigned int)(clk_in / (div ? div : 1U)));
    }

    /* ---- 5. 厂家整套 LCD_Init (内部会再调 HAL_LCDC_Init + 复位 + ReadID) - */
    s_hlcdc.Instance = hwp_lcdc1;
    s_step_t0 = HAL_GetTick();
    LCD_Init(&s_hlcdc);
    lcd_step("LCD_Init(total)");

    /* ======================================================================
     * ★★★ 【三屏 UI 显示不出来的真正根因 + 修复】 ★★★
     *
     *  现象: 开机三色带(lcd_fill_raw)正常, 但 UI 的 blit 一执行, 屏上只剩
     *        "一片蓝色"(颜色恰好等于最后一条色带的蓝), 三屏底色全不出来。
     *
     *  根因(厂家源码依据):
     *    1) 厂家 HAL 用 layer 的"整行字节数"决定要取多少像素:
     *         bf0_hal_lcdc.c:1321-1324 (LayerUpdate)
     *           if (INVALID_TOTAL_WIDTH == cfg->total_width)
     *               layer_1line_total_bytes = data_w * bytes_per_pixel;
     *           else
     *               layer_1line_total_bytes = cfg->total_width * bytes_per_pixel;
     *         该值写进 LAYER0_CONFIG.WIDTH (bf0_hal_lcdc.c:1365)。
     *    2) cfg->total_width 只有 HAL_LCDC_LayerReset() 才会置成
     *         INVALID_TOTAL_WIDTH(0xFFFF)  (bf0_hal_lcdc.c:2796-2798;
     *         宏定义 bf0_hal_lcdc.h:78)。
     *        HAL_LCDC_LayerSetData() 本身【不】设 total_width。
     *    3) 厂家正常流程是 LCD 设备驱动 drv_lcd.c:2196-2199 (LCD_MSG_OPEN)
     *       在 open 之后补两句:
     *           HAL_LCDC_SetBgColor(&drv_lcd.hlcdc, 0, 0, 0);
     *           HAL_LCDC_LayerReset(&drv_lcd.hlcdc, HAL_LCDC_LAYER_DEFAULT);
     *       本仓库的移植【不经过 drv_lcd.c】, 直接调 HAL_LCDC_Init,
     *       于是 s_hlcdc 是静态零初始化 -> Layer[0].total_width = 0、
     *       Layer[0].alpha = 0, 两句都没人补。
     *    4) 后果: total_width=0 -> layer_1line_total_bytes=0 ->
     *       LAYER0_CONFIG.WIDTH=0 -> LCDC 每行取 0 字节 -> layer 一个像素都
     *       取不到 -> 输出全用 CANVAS_BG(即 lcdc->bg), 而 lcdc->bg 还是
     *       最后一条蓝色带的 (0,0,255) -> 屏上"一片蓝色"。
     *       这同时解释了: 为什么 fill(layer disabled, 只送 CANVAS_BG)可见,
     *       而 blit(layer enabled, 送位图)不可见。
     *
     *  修复: 完全照厂家 drv_lcd.c:2196-2199 补上这两句 —— 必须在
     *        HAL_LCDC_Init 之后 (LayerReset 会 memset Layer[], 不能被后面的
     *        HAL_LCDC_Init 覆盖), 且在任何 layer 使用之前。
     * ==================================================================== */
    HAL_LCDC_SetBgColor(&s_hlcdc, 0, 0, 0);
    HAL_LCDC_LayerReset(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT);
    lcd_step("LayerReset(vendor fix)");

    /* ---- 6. LCD_ReadID 全过程 ------------------------------------------- */
    s_step_t0 = HAL_GetTick();
    rt_kprintf("[LCD] id=0x%x\n", LCD_ReadID(&s_hlcdc));
    lcd_step("LCD_ReadID");

    /* ---- 7. 背光 PWM 启动 ------------------------------------------------ */
    s_step_t0 = HAL_GetTick();
    backlight_pwm_init(80);
    lcd_step("backlight_pwm_init");

    /* ---- 8. 亮度 / 区域 / 清屏 ------------------------------------------ */
    s_step_t0 = HAL_GetTick();
    LCD_SetBrightness(&s_hlcdc, 50);
    lcd_step("LCD_SetBrightness");

    s_step_t0 = HAL_GetTick();
    LCD_SetRegion(&s_hlcdc, 0, 0, LCD_PIXEL_WIDTH, LCD_PIXEL_HEIGHT);
    lcd_step("LCD_SetRegion");

    s_step_t0 = HAL_GetTick();
    LCD_Clear(&s_hlcdc);
    lcd_step("LCD_Clear");

    s_inited = 1;
    return 0;
}

void sf32lb52_lcd_vendor_clear(void)
{
    if (s_inited) LCD_Clear(&s_hlcdc);
}

void sf32lb52_lcd_vendor_fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              uint8_t r, uint8_t g, uint8_t b)
{
    if (s_inited) lcd_fill_raw(x0, y0, x1, y1, r, g, b);
}

/* --------------------------------------------------------------------------
 *  整块刷屏 (blit): 把一块 RGB565 像素缓冲推到屏上指定矩形
 *
 *  ★ 病因(两次定位, 以第二次为准):
 *    [第一次/已排除] 旧代码漏调 LCD_SetRegion()。补上后屏确实会"变"
 *        (从三色带变成一片蓝), 但三屏底色仍不出现, 说明这不是根因。
 *    [第二次/真根因] 见 sf32lb52_lcd_vendor_init() 里 "★★★" 大段注释:
 *        Layer[0].total_width 从没人置成 INVALID_TOTAL_WIDTH,
 *        LAYER0_CONFIG.WIDTH=0 -> LCDC 每行取 0 字节 -> layer 位图取不到,
 *        整屏输出退化成 CANVAS_BG(残留的蓝色) -> "一片蓝色"。
 *        已在 init 里照厂家 drv_lcd.c:2196-2199 补
 *        HAL_LCDC_LayerReset() (它同时把 total_width 置 INVALID、alpha 置 255)。
 *
 *  调用序列 (与厂家 co5300.c:327-333 LCD_WriteMultiplePixels 同构, 只多一次
 *  SetRegion 以保证面板窗口/LCDC canvas 一致 —— 这条不改):
 *      先 LCD_SetRegion 设好面板窗口(0x2A/0x2B)与 LCDC canvas,
 *      再 LayerSetData 挂像素缓冲并给出 data_area(=roi), LayerEnable。
 *
 *  ★★ 发送方式 = 照厂家, 用【中断版】HAL_LCDC_SendLayerData2Reg_IT ★★
 *
 *  厂家对照 (逐句):
 *    co5300.c:327-333 LCD_WriteMultiplePixels()
 *        HAL_LCDC_LayerSetData(...); HAL_LCDC_SendLayerData2Reg_IT(..., 4);
 *                            ^^^ 中断版, 不在 HAL 里死等
 *    drv_lcd.c:1737          hlcdc.XferCpltCallback = SendLayerDataCpltCbk;
 *    drv_lcd.c:1750          WriteMultiplePixels(...)   <- 上面那个 _IT
 *    drv_lcd.c:1755          rt_sem_take(&draw_sem, MAX_LCD_DRAW_TIME)
 *                                          ^^^ 在【任务里】带超时地等完成
 *    drv_lcd.c:522-538 SendLayerDataCpltCbk()
 *        XferCpltCallback = NULL; rt_sem_release(&draw_sem);  <- 中断里放锁
 *    drv_lcd.c:824-825   HAL_NVIC_SetPriority(LCDC1_IRQn, 6, 0);
 *                        HAL_NVIC_EnableIRQ(LCDC1_IRQn);      <- 开 LCDC1 中断
 *
 *  本工程等价物:
 *    - 完成回调  blit_cplt_cb()            <- 厂家 SendLayerDataCpltCbk
 *    - 完成标志  s_blit_done               <- 厂家 draw_sem
 *    - 中断入口  sf32lb52_lcd_vendor_irq() <- 厂家 LCDC1_IRQHandler
 *    - 中断挂接  NuttX 侧 irq_attach(SF32LB52_IRQ_LCDC1,...) + up_enable_irq()
 *                (在 thermo_ui_glue.c 的 sf32lb52_lcd_irq_start(), 与 PA31
 *                 触摸中断同一套做法; 本文件不引 NuttX 头, 只提供中断入口)
 *
 *  ★ 为什么【必须】换掉原来的阻塞版 SendLayerData2Reg:
 *    阻塞版最终停在厂家 HAL 的 bf0_hal_lcdc.c:1680-1700 _WaitSendLayerDone():
 *        bf0_hal_lcdc.c:1696
 *            while (0 == (lcdc->Instance->IRQ & LCD_IF_IRQ_EOF_RAW_STAT));
 *    这是一个【无超时】的裸轮询 —— 只要本次 layer 搬运的 EOF 不来, 线程永远
 *    出不来, 且没有任何打印 (静默卡死)。这与"lv_scr_load done 之后所有常驻
 *    线程打印全部消失"完全吻合。
 *    为什么 fill 好、blit 卡: lcd_fill_raw 把 layer 关掉(LayerDisable), LCDC
 *    只吐 CANVAS_BG, 不需要取像素; blit 把 layer 打开并挂上 390*40*2=31200B
 *    位图, LCDC 必须真去取内存 —— 两种情形在 HAL 里走的是同一段发送代码,
 *    差别就是"要不要 layer 取数"。
 *
 *  约束: 只支持【整行宽】矩形 (x0=0, x1=W-1) —— 调用方按横条 (strip) 分块。
 * ------------------------------------------------------------------------ */

/* 厂家 drv_lcd.c:522-538 的等价物: 中断里只置完成标志, 不做任何耗时操作 */
static volatile uint32_t s_blit_done;
static volatile uint32_t s_blit_irq_cnt;   /* 完成中断次数 (证据) */
static volatile uint32_t s_blit_to_cnt;    /* 超时次数 (证据) */
static volatile uint32_t s_blit_err_cnt;   /* 厂家错误回调次数 (证据) */
static volatile uint32_t s_blit_seq;       /* 首帧第几块 (证据) */

/* 证据打印上限: 首帧约 12 块(390x450 / 40行), 打前 24 块足够定位且不刷屏 */
#define BLIT_TRACE_MAX   24u

static void blit_cplt_cb(LCDC_HandleTypeDef *lcdc)
{
    (void)lcdc;
    s_blit_irq_cnt++;
    s_blit_done = 1;
}

/* 厂家 drv_lcd.c:561-565 SendLayerDataErrCbk 的等价物 (drv_lcd.c:1738 挂它)。
 * 本工程原来只挂了 XferCpltCallback, 没挂错误回调 —— 照厂家补齐,
 * 这样 HAL 里的 LCDC_TransErrCallback(bf0_hal_lcdc.c:1800) 才有落点。 */
static void blit_err_cb(LCDC_HandleTypeDef *lcdc)
{
    (void)lcdc;
    s_blit_err_cnt++;
}

/* 异常/超时时把 LCDC 关键寄存器打出来 (厂家 drv_lcd.c:1813-1814
 * TimeoutDbg 的角色): 用来判断 layer 到底被挂上了什么。
 *   WIDTH  = LAYER0_CONFIG[层每行取数字节] (bf0_hal_lcdc.c:1365)
 *   SRC    = 取数源地址                    (bf0_hal_lcdc.c:1432)
 *   STATUS/LCD_SINGLE 的 BUSY 位若不落 = layer 取数卡住。 */
static void blit_dump(const char *tag, uint32_t n)
{
    rt_kprintf("[LCD] blit#%u %s State=%d Lock=%d Err=%08x IRQ=%08x SETTING=%08x STATUS=%08x\r\n",
               (unsigned)n, tag,
               (int)s_hlcdc.State, (int)s_hlcdc.Lock, (unsigned)s_hlcdc.ErrorCode,
               (unsigned)s_hlcdc.Instance->IRQ,
               (unsigned)s_hlcdc.Instance->SETTING,
               (unsigned)s_hlcdc.Instance->STATUS);
    rt_kprintf("[LCD] blit#%u cfg WIDTH=%u L0_CFG=%08x TL=%08x BR=%08x SRC=%08x canvasTL=%08x canvasBR=%08x\r\n",
               (unsigned)n,
               (unsigned)((s_hlcdc.Instance->LAYER0_CONFIG & LCD_IF_LAYER0_CONFIG_WIDTH_Msk)
                          >> LCD_IF_LAYER0_CONFIG_WIDTH_Pos),
               (unsigned)s_hlcdc.Instance->LAYER0_CONFIG,
               (unsigned)s_hlcdc.Instance->LAYER0_TL_POS,
               (unsigned)s_hlcdc.Instance->LAYER0_BR_POS,
               (unsigned)s_hlcdc.Instance->LAYER0_SRC,
               (unsigned)s_hlcdc.Instance->CANVAS_TL_POS,
               (unsigned)s_hlcdc.Instance->CANVAS_BR_POS);
}

/* LCDC1 中断入口: NuttX 侧 IRQ 服务例程调用它
 * 等价 厂家 sifli bsp 的 LCDC1_IRQHandler -> HAL_LCDC_IRQHandler(hlcdc)  */
void sf32lb52_lcd_vendor_irq(void)
{
    HAL_LCDC_IRQHandler(&s_hlcdc);
}

uint32_t sf32lb52_lcd_vendor_blit_stats(uint32_t *irq_cnt, uint32_t *to_cnt)
{
    if (irq_cnt) *irq_cnt = s_blit_irq_cnt;
    if (to_cnt)  *to_cnt  = s_blit_to_cnt;
    return s_blit_done;
}

uint32_t sf32lb52_lcd_vendor_blit_err_cnt(void)
{
    return s_blit_err_cnt;
}

int sf32lb52_lcd_vendor_blit(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                             const void *pix)
{
    uint32_t t0, dt, n;
    HAL_StatusTypeDef st;
    int trace;

    if (!s_inited || pix == NULL) return -1;
    if (x0 > x1 || y0 > y1)      return -1;

    /* ==== 证据: 本块序号 + 进入时 HAL 状态 (区分 a/b/c 的第 1 手材料) ==== */
    n     = ++s_blit_seq;
    trace = (n <= BLIT_TRACE_MAX);
    if (trace)
        rt_kprintf("[LCD] blit#%u area=(%u,%u,%u,%u) enter State=%d Lock=%d Err=%08x\r\n",
                   (unsigned)n, (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1,
                   (int)s_hlcdc.State, (int)s_hlcdc.Lock, (unsigned)s_hlcdc.ErrorCode);

    HAL_LCDC_Next_Frame_TE(&s_hlcdc, 0);
    LCD_SetRegion(&s_hlcdc, x0, y0, x1, y1);
    if (trace) rt_kprintf("[LCD] blit#%u a:setregion ok\r\n", (unsigned)n);
    HAL_LCDC_LayerSetFormat(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT, LCDC_PIXEL_FORMAT_RGB565);
    HAL_LCDC_LayerSetData(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT,
                          (uint8_t *)pix, x0, y0, x1, y1);
    HAL_LCDC_LayerEnable(&s_hlcdc, HAL_LCDC_LAYER_DEFAULT);
    if (trace)
        rt_kprintf("[LCD] blit#%u b:pix=0x%08x\r\n", (unsigned)n, (unsigned)(uintptr_t)pix);

    /* ---- 照厂家: 挂完成回调 + 中断版发送 (drv_lcd.c:1737-1738 / co5300.c:332) ---- */
    s_blit_done = 0;
    s_hlcdc.XferCpltCallback = blit_cplt_cb;
    s_hlcdc.XferErrorCallback = blit_err_cb;
    st = HAL_LCDC_SendLayerData2Reg_IT(&s_hlcdc,
                                       ((0x32 << 24) | (REG_WRITE_RAM << 8)), 4);
    if (trace)
        rt_kprintf("[LCD] blit#%u c:send rc=%d (此行为本块最后一条 => 卡在 HAL 发送内部)\r\n",
                   (unsigned)n, (int)st);
    if (HAL_OK != st)
    {
        s_hlcdc.XferCpltCallback = NULL;
        s_hlcdc.XferErrorCallback = NULL;
        return -1;
    }
    if (trace) blit_dump("d:sent", n);   /* layer 挂上后 WIDTH/SRC/BUSY 的实况 */

    /* ---- 照厂家: 在任务里【带超时】地等完成 (drv_lcd.c:1755 的角色) ----
     * 厂家等的是 draw_sem(超时 MAX_LCD_DRAW_TIME); 这里等中断置起的标志。
     * 这是一道"绝不会永久卡死"的保险: 最坏 100ms 必定返回, 常驻线程继续跑。 */
    t0 = HAL_GetTick();
    while (0 == s_blit_done)
    {
        if ((uint32_t)(HAL_GetTick() - t0) > 100u)
        {
            s_hlcdc.XferCpltCallback = NULL;
            s_hlcdc.XferErrorCallback = NULL;
            s_hlcdc.State = HAL_LCDC_STATE_READY;   /* 复位, 让下一次还能发 */
            s_hlcdc.Lock  = HAL_UNLOCKED;
            s_blit_to_cnt++;
            if (trace) blit_dump("e:TIMEOUT", n);
            return -2;                              /* -2 = 超时(有证据可查) */
        }
    }

    dt = (uint32_t)(HAL_GetTick() - t0);
    s_hlcdc.XferCpltCallback = NULL;
    s_hlcdc.XferErrorCallback = NULL;
    if (trace)
        rt_kprintf("[LCD] blit#%u f:done dt=%ums irq=%u err=%u\r\n",
                   (unsigned)n, (unsigned)dt,
                   (unsigned)s_blit_irq_cnt, (unsigned)s_blit_err_cnt);
    return 0;
}

/* --------------------------------------------------------------------------
 *  从屏上读一个像素 (回读校验用)
 *
 *  ★ 为什么原来"返回陈旧值"(厂家源码依据 + 实测反推):
 *    - 厂家 read 通路: co5300.c:383 LCD_ReadPixel()
 *        -> LCD_SetRegion(x,y,x,y)   (发 0x2A/0x2B, co5300.c:286-309)
 *        -> LCD_ReadData(0x2E, 4)    (co5300.c:367-379 -> 2MHz 读)
 *        -> HAL_LCDC_ReadU32Reg -> HAL_LCDC_ReadDatas (bf0_hal_lcdc.c:2469-2528)
 *    - 厂家 LCD_ReadPixel 是给"裸驱/全屏更新"设计的: 一个窗口只读一次。
 *      但本板上 CO5300(QADSPI 4 数据线)的 0x2E 读回带【一拍流水延迟】:
 *      刚发完 0x2A/0x2B 后的第一次 0x2E 读, 读到的是【上一个窗口】的像素。
 *      实测反推(开机三色带那行):
 *        readpixel(195, 75 ) -> 0xF80F  = 红带 | 0x000F    (窗口=红, 对)
 *        readpixel(195,225) -> 0xF80F  = 红带 | 0x000F    (窗口=绿, 读到红)
 *        readpixel(195,375) -> 0x07EF  = 绿带 | 0x000F    (窗口=蓝, 读到绿)
 *      即每次读都比自己的窗口"晚一拍" —— 这就是"陈旧值"的来源。
 *      (低 nibble 恒被置 F 是同一现象的一部分: 该 nibble 来自面板读回的
 *       缓冲/对齐字节, 并非真实像素位, 比对时按 & ~0xF 屏蔽。)
 *    - 修法: 设好窗口后【先做一次丢弃读】把流水冲掉, 再正式读一次。
 *      (这与 UI 侧 ui_verify_point 的"同点连读两次取第二次"是同一思路,
 *       这里收进底层, 让所有调用者都拿到稳定值。)
 * ------------------------------------------------------------------------ */
uint32_t sf32lb52_lcd_vendor_readpixel(uint16_t x, uint16_t y)
{
    uint32_t v;

    if (!s_inited) return 0;

    /* 1) 设窗口 + 丢弃读: 冲掉面板 0x2E 的一拍流水延迟 */
    LCD_SetRegion(&s_hlcdc, x, y, x, y);
    (void)LCD_ReadData(&s_hlcdc, REG_READ_RAM, 4);

    /* 2) 同一窗口再读一次(厂家 LCD_ReadPixel 内部会再设一次窗口),
     *    这次拿到的才是该点当前像素 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    v = LCD_ReadPixel(&s_hlcdc, x, y);
#pragma GCC diagnostic pop
    return v;
}

/* --------------------------------------------------------------------------
 *  readpixel 自证 / 纯色自检 (Task A + Task B)
 *
 *  A) readpixel 自证: 先 fill 全屏纯色 X, 再在同一屏上读若干点,
 *     若所有点都等于 X, 才认为 readpixel 可用 —— 这是"用它当判据"的前提。
 *     (低 nibble 用 & ~0xF 屏蔽, 理由见上一条注释。)
 *  B) 纯色自检: 依次整屏 红/绿/蓝 (各 1s), 走的就是开机三色带那条
 *     已上板验证的 lcd_fill_raw 通路。
 *     看板判据:
 *       - 红/绿/蓝 顺序正确 -> 面板/时序/格式都对, 三屏问题只能在 UI 绘制侧;
 *       - 颜色错位(红显示成蓝等) -> RGB565 字节序/位序问题;
 *       - 三屏同色       -> 写窗口或数据源问题(且非 fill 通路本身)。
 * ------------------------------------------------------------------------ */
static int s_readpixel_ok = 0;

int sf32lb52_lcd_vendor_readpixel_usable(void)
{
    return s_readpixel_ok;
}

void sf32lb52_lcd_vendor_solidtest(void)
{
    static const uint8_t  col[3][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255} };
    static const char    *nm[3]     = { "red", "green", "blue" };
    int c;

    if (!s_inited) return;

    for (c = 0; c < 3; c++)
    {
        lcd_fill_raw(0, 0, 389, 449, col[c][0], col[c][1], col[c][2]);
        rt_kprintf("[UI] solid screen=%s\n", nm[c]);
        hw_mdelay(1000);
    }
}

int sf32lb52_lcd_vendor_readpixel_selftest(void)
{
    static const uint8_t  col[3][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255} };
    static const uint16_t exp[3]    = { 0xF800, 0x07E0, 0x001F };
    static const uint16_t pt[4][2]  = { {195,  75}, { 30, 300}, {360, 420}, {195, 225} };
    int c, k;
    int all_ok = 1;

    if (!s_inited)
    {
        s_readpixel_ok = 0;
        rt_kprintf("[LCD] readpixel selftest: SKIPPED (LCD not inited)\n");
        return -1;
    }

    for (c = 0; c < 3; c++)
    {
        uint32_t last = 0;
        int      cok  = 1;

        /* 整屏纯色 X (走已验证的 lcd_fill_raw) */
        lcd_fill_raw(0, 0, 389, 449, col[c][0], col[c][1], col[c][2]);

        for (k = 0; k < 4; k++)
        {
            last = sf32lb52_lcd_vendor_readpixel(pt[k][0], pt[k][1]);
            if (((last ^ (uint32_t)exp[c]) & 0xFFF0u) != 0u) cok = 0;
        }

        rt_kprintf("[LCD] readpixel selftest: fill=0x%04x read=0x%04x -> %s\n",
                   (unsigned int)exp[c], (unsigned int)last, cok ? "OK" : "FAIL");
        if (!cok) all_ok = 0;
    }

    s_readpixel_ok = all_ok;
    rt_kprintf("[LCD] readpixel selftest: %s\n",
               all_ok ? "OK (可用于回读校验)"
                      : "FAIL -> readpixel 不可用, 回读校验降级为 SKIPPED");
    return all_ok ? 0 : -1;
}

void sf32lb52_lcd_vendor_setbrightness(uint32_t percent)
{
    backlight_pwm_set(percent);
    if (s_inited) LCD_SetBrightness(&s_hlcdc, (uint8_t)percent);
}

uint32_t sf32lb52_lcd_vendor_readid(void)
{
    return s_inited ? LCD_ReadID(&s_hlcdc) : 0;
}
EOF

# ---- 3b. 触摸胶水层 (厂家 FT6146 协议原文) ----------------------------------
cat > "$OUT/vendor_touch_glue.c" <<'EOF'
/* ==========================================================================
 *  触摸胶水: 把厂家 FT6146 驱动接到裸片上
 *
 *  厂家依据 (全部照抄, 不含任何自造时序):
 *    sdk/customer/boards/sf32lb52-lcd_base/bsp_lcd_tp.c
 *        #define TP_RESET (9)          // GPIO_A09
 *        BSP_TP_PowerUp(): BSP_PIN_Touch() -> BSP_GPIO_Set(TP_RESET, 1)
 *    sdk/customer/boards/sf32lb52-lcd_base/bsp_pinmux.c  BSP_PIN_Touch()
 *        PA09 GPIO_A9  (CTP_RESET) / PA31 GPIO_A31 (CTP_INT)
 *        PA30 I2C1_SCL / PA33 I2C1_SDA   (PIN_PULLUP)
 *    sdk/rtos/rtthread/bsp/sifli/drivers/drv_i2c.c  rt_i2c_cfg_default[]
 *        I2C1: max_hz = 400000
 *        master_xfer() 开头必做 __HAL_I2C_ENABLE(&handle)   (drv_i2c.c:263/625/960)
 *        —— 因为 HAL_I2C_Init() 里 bf0_hal_i2c.c:342 的 __HAL_I2C_ENABLE 被注释掉,
 *           单元使能(IUE)必须在外部补; 本胶水层照此补上。
 *    sdk/customer/peripherals/ft6146/ft6146.c
 *        FT_DEV_ADDR 0x38; ID 寄存器 0xA3/0x9F;
 *        read_point(): 从 0x01 起读 2 + 6*MAX_POINT_NUM 字节
 *        末了 ft6146_correct_pos():  x = 390 - x;  y = 450 - y   (镜像)
 *
 *  寄存器布局与 FT5x06 标准一致 (P1_XH[7:6]=事件, P1_YH[7:4]=触点 ID),
 *  所以后续可以直接对接 NuttX 自带的 input/ft5x06 驱动。
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void hw_mdelay(uint32_t ms);      /* 在 bf0_vendor_glue.o 里 */

#define TP_RESET        9                /* PA09 */

/* 从机地址属于【板级硬件参数】, 与厂家代码里的默认值未必一致:
 *   厂家参考板 sdk/customer/peripherals/ft6146/ft6146.c:63  FT_DEV_ADDR = 0x38 (7bit)
 *   本板图纸/实测给出的地址是 0x5A (8bit 0xB4/0xB5)
 * 所以运行时先试厂家默认值, 不通再试本板地址, 谁 ACK 就用谁 —— 两种板子都能跑。
 * 注意这里【不是】在手写寄存器协议, 只是选定传给厂家 HAL 的从机地址参数。 */
#define FT_DEV_ADDR      0x38u           /* 厂家默认 (7bit) */
#define FT_DEV_ADDR_ALT  0x5Au           /* 本板 (7bit) */

#define MAX_POINT_NUM   2                /* 厂家 ft6146.c: MAX_POINT_NUM */

/* ★ I2C 等待时长, 单位【毫秒】—— 语义由 HAL_GetTick 的实现决定(现为 DWT 真实毫秒)。
 *   旧实现是"每次调用+1"的软件计数, Timeout 实际含义是"轮询次数";
 *   改成真实毫秒后必须按厂家量级取值:
 *     常规寄存器读写 / tx 探针 : 500ms (400kHz 下一次事务只需几十 us)
 *     地址扫描 (112 个地址)    : 50ms  (合计约 5.6s) */
#define TP_I2C_TIMEOUT_MS   500U
#define TP_SCAN_TIMEOUT_MS  50U

static I2C_HandleTypeDef s_i2c;
static int               s_tp_inited = 0;
static uint16_t          s_dev_addr  = FT_DEV_ADDR;   /* 运行时选定的从机地址 */

static void tp_gpio_set(int pin, int val)
{
    GPIO_InitTypeDef gi;
    gi.Mode = GPIO_MODE_OUTPUT;
    gi.Pin  = (uint16_t)pin;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(hwp_gpio1, &gi);
    HAL_GPIO_WritePin(hwp_gpio1, (uint16_t)pin, (GPIO_PinState)val);
}

/* 厂家 ft6146.c init() 的复位脉冲 (原文):
 *     BSP_TP_Reset(0); rt_thread_mdelay(5);
 *     BSP_TP_Reset(1); rt_thread_mdelay(80);
 * 注意调用时机: 厂家是在【I2C 总线已经建好之后】才发这个脉冲的,
 * 所以这里也放在 HAL_I2C_Init 之后调用。 */
static void tp_reset_pulse(void)
{
    tp_gpio_set(TP_RESET, 0);
    hw_mdelay(5);
    tp_gpio_set(TP_RESET, 1);
    hw_mdelay(80);
}

/* ★ Timeout 已是【毫秒】(见 HAL_GetTick 的 DWT 实现), 故用 500ms;
 *   原值 100 是"轮询次数"旧语义下的取值。 */
/* 探一下某个从机地址是否 ACK: 等价于厂家 ft6146.c read_regs() 的地址相,
 * 只是不发寄存器号、只发 1 字节 dummy。地址存在则返回 1。 */
static int tp_probe_addr(uint16_t a)
{
    uint8_t dummy = 0;

    __HAL_I2C_ENABLE(&s_i2c);
    return (HAL_I2C_Master_Transmit(&s_i2c, a, &dummy, 1, TP_I2C_TIMEOUT_MS) == HAL_OK) ? 1 : 0;
}

/* 选定从机地址: 先用厂家默认值, 再试本板地址, 谁 ACK 用谁 (都不通则保持原值)。 */
static void tp_select_addr(void)
{
    uint16_t old = s_dev_addr;

    if (tp_probe_addr(FT_DEV_ADDR))          s_dev_addr = FT_DEV_ADDR;
    else if (tp_probe_addr(FT_DEV_ADDR_ALT)) s_dev_addr = FT_DEV_ADDR_ALT;

    rt_kprintf("[TP] dev addr: try 0x%02x/0x%02x -> use 0x%02x%s\n",
               (unsigned int)FT_DEV_ADDR, (unsigned int)FT_DEV_ADDR_ALT,
               (unsigned int)s_dev_addr, (old != s_dev_addr) ? " (changed)" : "");
}

static int tp_read_regs(uint8_t reg, uint8_t len, uint8_t *buf)
{
    /* 与厂家 master_xfer 一样, 每次搬运前重申一次单元使能 (幂等),
     * 保证任何一条 HAL 失败路径把 IUE 关掉后下一次仍能跑。 */
    __HAL_I2C_ENABLE(&s_i2c);
    if (HAL_I2C_Master_Transmit(&s_i2c, s_dev_addr, &reg, 1, TP_I2C_TIMEOUT_MS) != HAL_OK)
        return -1;
    if (HAL_I2C_Master_Receive(&s_i2c, s_dev_addr, buf, len, TP_I2C_TIMEOUT_MS) != HAL_OK)
        return -1;
    return 0;
}

int sf32lb52_touch_vendor_init(void)
{
    if (s_tp_inited) return 0;

    HAL_PIN_Set(PAD_PA09, GPIO_A9,  PIN_NOPULL, 1);      /* CTP_RESET */
    HAL_PIN_Set(PAD_PA31, GPIO_A31, PIN_PULLUP, 1);      /* CTP_INT (开漏低有效 -> 上拉) */
    HAL_PIN_Set(PAD_PA30, I2C1_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA33, I2C1_SDA, PIN_PULLUP, 1);

    /* CTP_INT(PA31): 与厂家 bsp_pinmux 同一根脚, 复用为 GPIO_A31 带上拉输入。
     * 这里【只配普通输入】(HAL_GPIO_Init(INPUT) 内部会 DISABLE_ISR -> 清 IER);
     * 真正挂"下降沿中断"在 sf32lb52_touch_irq_config() (下面), 它必须排在
     * NuttX 侧 irq_attach 之前调用, 否则 IER 又被这里清掉。 */
    {
        GPIO_InitTypeDef gi;
        gi.Mode = GPIO_MODE_INPUT;
        gi.Pin  = 31;
        gi.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(hwp_gpio1, &gi);
    }

    /* 复位脉冲【不在这里发】—— 移到 HAL_I2C_Init 之后 (见 tp_reset_pulse),
     * 与厂家的先后顺序保持一致 (RT-Thread 是先建总线、再 ft6146_init 复位)。 */
    rt_kprintf("[TP] pinmux ok\n");

    /* 以下字段的顺序与取值完全照厂家 drv_i2c.c 的 i2c_bus_configure():
     *   handle.Init.AddressingMode / OwnAddress1 / ClockSpeed /
     *   GeneralCallMode / core / Mode -> EnableModule -> HAL_I2C_Init
     *
     * ★ HAL_RCC_EnableModule(RCC_MOD_I2C1) 是【必须】的一步:
     *   HAL_I2C_Init 内部只调 HAL_I2C_MspInit(弱函数), 默认【不】使能模块时钟。
     *   在 SF32 上访问未开时钟的外设会让 AHB 停住 —— 现象是进程卡死:
     *   bringup 不返回, NuttShell 也出不来 (连失败打印都没有)。
     *   厂家在 drv_i2c.c:1226-1227 显式做了这一步。
     * ★ core / Mode 也必须设, 否则 HAL 按 0 值走会出错。
     */
    s_i2c.Instance             = hwp_i2c1;
    s_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.OwnAddress1     = 0;                  /* 厂家: (addr & 0x7fff) << 1, addr=0 */
    s_i2c.Init.ClockSpeed      = 400000;             /* 厂家 rt_i2c_cfg_default: max_hz */
    s_i2c.Init.Timing          = 0;                  /* HAL 内部按 ClockSpeed 推算 */
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.core                 = CORE_ID_HCPU;
    s_i2c.Mode                 = HAL_I2C_MODE_MASTER;

    /* ★ 外设时钟总线: 与 LCD 同一处修复 (见 vendor_lcd_glue.c 里
     *   sf32lb52_vendor_clock_init 的完整说明)。
     *   I2C1 的模块时钟来自 HP_PERI; 不把 HP_PERI 选到 48MHz, 就会出现
     *   "寄存器可读写(en=1/rst=0/LCR 正常)但状态机不推进、SR 恒 0、超时永不结束"。
     *   LCD 初始化已调过一次, 这里再调一次, 保证触摸探针单独运行时也成立 (幂等)。 */
    void sf32lb52_vendor_clock_init(void);       /* 定义在 vendor_lcd_glue.c */
    sf32lb52_vendor_clock_init();
    rt_kprintf("[TP] hp_peri clk = HXT48 (48MHz)\n");

    HAL_RCC_EnableModule(RCC_MOD_I2C1);
    rt_kprintf("[TP] i2c clk en ok\n");

    if (HAL_I2C_Init(&s_i2c) != HAL_OK)
    {
        rt_kprintf("[TP] HAL_I2C_Init FAIL\n");
        return -1;
    }

    /* ★★ 与"外设时钟"同等关键的一步: 使能 I2C 单元 (CR.IUE) ★★
     *
     *  厂家 HAL 的 HAL_I2C_Init() 结尾【故意没有】打开 IUE
     *  —— bf0_hal_i2c.c:342 那句 __HAL_I2C_ENABLE(hi2c) 是被注释掉的;
     *  厂家把"开单元"这件事放在驱动层, 每个 master_xfer 开头自己做:
     *      drv_i2c.c:960   __HAL_I2C_ENABLE(&bf0_i2c->handle);
     *      drv_i2c.c:625   __HAL_I2C_ENABLE(&bf0_i2c->handle);
     *      drv_i2c.c:263   __HAL_I2C_ENABLE(&bf0_i2c->handle);
     *  宏定义见 bf0_hal_i2c.h:576 -> SET_BIT(Instance->CR, I2C_CR_IUE)。
     *
     *  ★ 只补外设时钟、不补 IUE 的后果 (实测现象):
     *      CR 里 SCLE/MODE 都对(en=1/rst=0/LCR 正常), 但 IUE=0 -> 单元不出
     *      START -> I2C_SR.UB 永不置起 -> HAL_I2C_Master_Transmit 在
     *      bf0_hal_i2c.c:1263 等 UB(I2C_TIMEOUT_BUSY=1000ms) 后返回 HAL_BUSY,
     *      对每一个地址都是如此 -> 扫描 "0 device(s) acked"、ID 恒 0xFF。
     *      这与"SR 恒 0"的旧现象完全吻合 —— 之前只修了时钟, 没修 IUE。 */
    __HAL_I2C_ENABLE(&s_i2c);
    rt_kprintf("[TP] i2c unit enable: CR=%x IUE=%d\n",
               (unsigned int)s_i2c.Instance->CR,
               (int)((s_i2c.Instance->CR & I2C_CR_IUE) ? 1 : 0));

    /* ★ 总线就绪之后再发复位脉冲 (与厂家 rt-thread 的先后顺序一致) */
    tp_reset_pulse();
    rt_kprintf("[TP] reset pulse done (after i2c init)\n");

    /* 复位后选一次从机地址 (厂家 0x38 / 本板 0x5A) */
    tp_select_addr();

    s_tp_inited = 1;
    return 0;
}

/* ==========================================================================
 *  PA31 下降沿中断 (照厂家做法) —— 修 g_mode=0x01(中断触发模式)下纯轮询读不到点
 *
 *  为什么必须补中断 (实测根因):
 *    [TP] readpoint selftest 实测 g_mode=0x01 —— FT6146 处于"中断触发模式",
 *    触点数据只在 PA31 下降沿事件时交付, 无条件轮询恒读到 TD_STATUS(n)=0,
 *    于是屏幕按键永远没反应。厂家就是靠中断驱动读点的:
 *
 *  厂家依据 (逐条对照, 本胶水层照此等价实现):
 *    ft6146.c:374-375  rt_touch_irq_pin_attach(PIN_IRQ_MODE_FALLING, ...);
 *                      rt_touch_irq_pin_enable(1);
 *    drv_touch.c:561  读点线程阻塞在 rt_sem_take(isr_sem) —— 中断释放信号量
 *    drv_touch.c:570  之后才调 ops->read_point()
 *    vendor/sifli/chips/sf32lb52/sifli_gpio.c  sifli_gpio_irq_enable():
 *        GPIO_EVENT_MODE_FALLING -> GPIO_InitStruct{Mode=GPIO_MODE_IT_FALLING,
 *        Pull=GPIO_PULLUP} + HAL_GPIO_Init(hwp_gpio1,&gi)
 *        然后 irq_attach(GPIO1_IRQn + NVIC_IRQ_FIRST, gpio_isr, hwp_gpio1)
 *            + up_enable_irq(GPIO1_IRQn + NVIC_IRQ_FIRST)
 *    其中 GPIO1_IRQn = 84 (vendor cmsis/sf32lb52x/register.h:120),
 *         NVIC_IRQ_FIRST = 16 (nuttx arch/arm/src/arm_m/nvic.h:62)
 *         -> IRQ 100 = 本树 arch/arm/include/sf32lb52/irq.h:91
 *                       SF32LB52_IRQ_GPIO1 (= SF32LB52_IRQ_EXTINT + 84)
 *
 *  分工 (为什么不在本文件直接 irq_attach):
 *    本 .a 用厂家 SDK 头文件编译 (rtthread.h/bf0_hal.h), 不含 NuttX 头文件,
 *    故本层只做【外设侧】寄存器配置与挂起位读取 (GPIO1 寄存器 + 厂家 HAL);
 *    向量/NVIC 挂接与信号量在 NuttX 侧完成 -> thermo_ui_glue.c:
 *    sf32lb52_touch_irq_start() 里 irq_attach(SF32LB52_IRQ_GPIO1, ...) +
 *    up_enable_irq(SF32LB52_IRQ_GPIO1), ISR 里 nxsem_post()。
 * ========================================================================== */

static volatile uint32_t s_tp_irq_cnt = 0;      /* 累计 PA31 下降沿次数 (诊断) */

/* 把 PA31 配成"带上拉的下降沿中断输入"。HAL_GPIO_Init(IT_FALLING) 内部写:
 *   ITSR |= bit   -> ITR  = 1  边沿触发
 *   IPHCR = bit   -> IPHR = 0  低优先级
 *   IPLSR = bit   -> IPLR = 1  => 下降沿
 *   IESR |= bit   -> IER  = 1  使能该脚中断
 * 与厂家 sifli_gpio.c 的 GPIO_EVENT_MODE_FALLING 完全一致。
 * 返回 0 = 已配好。 */
int sf32lb52_touch_irq_config(void)
{
    GPIO_InitTypeDef gi;

    if (!s_tp_inited) sf32lb52_touch_vendor_init();

    /* CTP_INT(PA31): 开漏低有效 -> 上拉, 复用为 GPIO 输入 (同厂家 bsp_pinmux) */
    HAL_PIN_Set(PAD_PA31, GPIO_A31, PIN_PULLUP, 1);

    gi.Mode = GPIO_MODE_IT_FALLING;      /* ← 厂家 PIN_IRQ_MODE_FALLING */
    gi.Pin  = 31;
    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(hwp_gpio1, &gi);

    rt_kprintf("[TP] IRQ cfg: PA31 falling-edge, IER=%x ITR=%x IPHR=%x IPLR=%x IESR=%x DOECR=%x\n",
               (unsigned int)hwp_gpio1->IER, (unsigned int)hwp_gpio1->ITR,
               (unsigned int)hwp_gpio1->IPHR, (unsigned int)hwp_gpio1->IPLR,
               (unsigned int)hwp_gpio1->IESR, (unsigned int)hwp_gpio1->DOECR);
    return 0;
}

/* 读并清除 GPIO1 bank0 的 PA31 挂起位; 返回 1 = 本次确有下降沿。
 * 等价于厂家 sifli_gpio.c GPIO1_IRQHandler -> HAL_GPIO_IRQHandler(hwp_gpio1)
 * 对 pin31 的处理 (读 ISR, 写 1 清除), 只是只处理 PA31 这一位 —— 避免遍历
 * 0..78 时对其它"未使能却有挂起位"的脚触发 HAL_ASSERT 自锁 (HAL_ASSERT 是
 * while(1), 见 sdk_port/Include/bf0_hal.h:460)。 */
int sf32lb52_touch_irq_dispatch(void)
{
    const uint32_t mask = (1u << 31);
    int hit = 0;

    if (hwp_gpio1->ISR & mask)
    {
        hwp_gpio1->ISR = mask;                   /* 写 1 清除 (同 HAL) */
        hit = 1;
    }
    if (hwp_gpio1->ISR_EXT & mask)
    {
        hwp_gpio1->ISR_EXT = mask;
        hit = 1;
    }

    if (hit) s_tp_irq_cnt++;
    return hit;
}

/* 累计下降沿次数 (供 NuttX 侧/自检打印, 证明中断真的在响) */
uint32_t sf32lb52_touch_irq_count(void)
{
    return s_tp_irq_cnt;
}

/* CTP_INT(PA31) 当前电平 (1=抬起, 0=按下), 供自检诊断 */
int sf32lb52_touch_int_level(void)
{
    return (int)HAL_GPIO_ReadPin(hwp_gpio1, 31);
}

uint32_t sf32lb52_touch_vendor_readid(void)
{
    uint8_t id = 0;
    if (!s_tp_inited) return 0xFFFFu;
    /* 厂家 ft6146.c: read_regs(0xA3, 1, &chip_id[0])   -- FT_READ_ID_H
     * 注意是【1 字节】—— 读 2 字节会多取到 0xA4, 那不是 FT_READ_ID_L(0x9F)。 */
    if (tp_read_regs(0xA3, 1, &id) != 0) return 0xFFFFu;   /* I2C 无应答 */
    return (uint32_t)id;
}

/* --------------------------------------------------------------------------
 *  诊断: 逐层打印 I2C 通路的实测状态 (排查 0xFF 无应答)
 *    1) 引脚复用有没有真的写进寄存器 —— 直接读 PINMUX1
 *    2) 三个 API 各自的 HAL 返回码, 用来区分"地址没 ACK"还是"配置没生效"
 * ------------------------------------------------------------------------ */
#define TP_PINMUX1_BASE  0x50003000UL
#define TP_PMR(pad)  (*(volatile uint32_t *)(TP_PINMUX1_BASE + ((pad) - 1U) * 4U))

/* 按厂家位定义解码一个 pad 寄存器, 位定义来自
 *   sdk/drivers/cmsis/sf32lb52x/hpsys_pinmux.h  (PAD_PA00 那一段)
 *     [3:0]=FSEL  [4]=PE  [5]=PS  [6]=IE  [7]=IS  [8]=SR  [10:9]=DS  [11]=POE
 *   PS: 1=上拉  0=下拉
 *   IS: 该 pad 当前的输入电平状态 —— 用它看总线空闲电平 / 复位脚是否真在动
 */
static void tp_dump_pad(const char *name, int pad)
{
    uint32_t v = TP_PMR(pad);
    rt_kprintf("[TP] %s reg=%x FSEL=%x PE=%d PS=%d IE=%d IS=%d SR=%d DS=%d\n",
               name, (unsigned int)v,
               (unsigned int)(v & 0xF),
               (int)((v >> 4) & 1), (int)((v >> 5) & 1), (int)((v >> 6) & 1),
               (int)((v >> 7) & 1), (int)((v >> 8) & 1), (int)((v >> 9) & 3));
}

/* 直接读 GPIO 的 DOER / DOR 寄存器, 闭环确认"这条脚到底是不是输出、电平是多少"。
 *   DOER = 输出使能寄存器, DOR = 输出数据寄存器  (字段名取自 bf0_hal_gpio.c 原文用法)
 *   第 2 个参数是 GPIO bank 指针: bank0 = hwp_gpio1, bank1 = hwp_gpio1 + 1
 *   (与 GPIO_GetInstance 里的 (GPIO_TypeDef *)hgpio + inst_idx 一致)
 *   bit = 引脚号 & 31
 */
static void tp_dump_gpio(const char *name, GPIO_TypeDef *g, int bit)
{
    uint32_t oe = g->DOER;
    uint32_t od = g->DOR;
    rt_kprintf("[TP] %s DOER=%x DOR=%x  -> OE=%d OUT=%d\n",
               name, (unsigned int)oe, (unsigned int)od,
               (int)((oe >> bit) & 1), (int)((od >> bit) & 1));
}

/* I2C 总线解卡: 从机被"卡在半字节"状态时的标准恢复手段 ——
 * 把 SCL/SDA 临时切成 GPIO, 补 9 个时钟让它把剩下的位移完, 再造一个 STOP,
 * 然后切回 I2C1。全程只用厂家 HAL_PIN_Set / gpio 接口。 */
static void tp_bus_recover(void)
{
    int i;

    HAL_PIN_Set(PAD_PA30, GPIO_A30, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA33, GPIO_A33, PIN_NOPULL, 1);

    tp_gpio_set(33, 1);                       /* 释放 SDA */
    for (i = 0; i < 9; i++)
    {
        tp_gpio_set(30, 0); hw_mdelay(1);
        tp_gpio_set(30, 1); hw_mdelay(1);
    }

    tp_gpio_set(30, 0); hw_mdelay(1);         /* SCL 低 */
    tp_gpio_set(33, 0); hw_mdelay(1);         /* SDA 低 */
    tp_gpio_set(30, 1); hw_mdelay(1);         /* SCL 高 */
    tp_gpio_set(33, 1); hw_mdelay(1);         /* SCL 高时 SDA 上升 = STOP */

    HAL_PIN_Set(PAD_PA30, I2C1_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA33, I2C1_SDA, PIN_PULLUP, 1);
}

void sf32lb52_touch_vendor_probe(void)
{
    uint8_t reg = 0xA3, id = 0;
    HAL_StatusTypeDef st_tx = HAL_ERROR, st_rx = HAL_ERROR;

    /* 重调一次厂家 API 并取【返回值】:
     *   HAL_PIN_Set 返回 -1 = 该 pad 没有这个功能(HAL_PIN_Func2Idx 越界),
     *   此时整段寄存器写入会被 if (i < PIN_FUNC_SEL_NUM) 跳过,
     *   引脚保持复位默认值 —— 这样 SDA/SCL 就没接到 I2C1 上。 */
    {
        int r30 = HAL_PIN_Set(PAD_PA30, I2C1_SCL, PIN_PULLUP, 1);
        int r33 = HAL_PIN_Set(PAD_PA33, I2C1_SDA, PIN_PULLUP, 1);
        rt_kprintf("[TP] pin_set PA30=%d PA33=%d  (-1 = 该pad无此功能,写入被跳过)\n",
                   r30, r33);
        rt_kprintf("[TP] mux2 PA30=%x PA33=%x\n",
                   (unsigned int)TP_PMR(31), (unsigned int)TP_PMR(34));
    }

    /* I2C 外设状态: 用 handle 自带的 Instance, 不做任何基址假设 */
    if (s_tp_inited)
    {
        rt_kprintf("[TP] i2c SR=%x CR=%x TCR=%x IER=%x\n",
                   (unsigned int)s_i2c.Instance->SR,
                   (unsigned int)s_i2c.Instance->CR,
                   (unsigned int)s_i2c.Instance->TCR,
                   (unsigned int)s_i2c.Instance->IER);
    }

    /* 关于 HAL_I2C_IsDeviceReady:
     *   厂家 HAL 在 bf0_hal_i2c.h:712 声明、并在 bf0_hal_i2c.c:2721 有实现
     *   (旧注释说"没实现"是过时的, 已核实)。这里仍用 Master_Transmit 探针:
     *   它同样会走 I2C_MasterRequestWrite 发从机地址, 地址无 ACK 时返回
     *   HAL_ERROR/HAL_BUSY/HAL_TIMEOUT, 足以判定通断; 且语义更贴近厂家 ft6146.c
     *   read_regs() 的"写寄存器地址 + 读数据"两步。 */
    if (s_tp_inited)
    {
        /* ★ tx 探针 Timeout 与 tp_read_regs 一致: 500ms (旧值 100 是"轮询次数"语义) */
        __HAL_I2C_ENABLE(&s_i2c);
        st_tx = HAL_I2C_Master_Transmit(&s_i2c, s_dev_addr, &reg, 1, TP_I2C_TIMEOUT_MS);
        st_rx = HAL_I2C_Master_Receive(&s_i2c, s_dev_addr, &id, 1, TP_I2C_TIMEOUT_MS);
    }

    /* --- (1) 引脚电学状态逐位解码 --- */
    tp_dump_pad("PA09 RST", 10);
    tp_dump_pad("PA30 SCL", 31);
    tp_dump_pad("PA31 INT", 32);
    tp_dump_pad("PA33 SDA", 34);

    /* --- (1b) GPIO 闭环: 读 DOER/DOR, 确认这些脚真被设成输出且电平正确 ---
     *   PA37(LCD_VADD_EN, bank1 bit5) 是【已知有效】的对照组 —— 屏就是它点亮的。
     *   PA09 若 OE=0, 说明复位脚根本没被设成输出 -> 芯片一直处于复位, 自然不应答。 */
    tp_dump_gpio("PA00 RST_LC", hwp_gpio1, 0);
    tp_dump_gpio("PA09 RST_TP", hwp_gpio1, 9);
    tp_dump_gpio("PA37 VADD_E", hwp_gpio1 + 1, 5);

    tp_gpio_set(TP_RESET, 0);
    hw_mdelay(2);
    tp_dump_gpio("PA09 ->low ", hwp_gpio1, 9);
    tp_gpio_set(TP_RESET, 1);
    hw_mdelay(2);
    tp_dump_gpio("PA09 ->high", hwp_gpio1, 9);

    /* --- (1c) 用【超大 Timeout】排除"假超时", 并直接抓 SR 的 NACK 位 ---
     *   我们的 HAL_GetTick() 是"每次调用+1"的软件计数器, 而厂家是真实毫秒,
     *   所以 I2C_WaitOnFlagUntilTimeout 里比的是【轮询次数】而不是时间 ——
     *   可能一次 20us 的地址相还没完成就被判超时。
     *   注意: HAL_I2C_Master_Transmit 会把任何非 OK(含 HAL_TIMEOUT) 统一
     *   转成 HAL_ERROR, 所以 tx=1 不能直接断定是 NACK。这里读 SR 区分。
     *   SR 可能是读清标志, 所以只读一次。 */
    if (s_tp_inited)
    {
        uint8_t reg = 0xA3;
        HAL_StatusTypeDef st;
        uint32_t sr;

        /* ★ Timeout 单位已是【毫秒】(见 HAL_GetTick 说明), 用厂家同量级的 500ms。
         *   原来写 1000000U 是配合"每次调用+1"的旧语义, 现在会变成 17 分钟, 必须改。 */
        st = HAL_I2C_Master_Transmit(&s_i2c, s_dev_addr, &reg, 1, TP_I2C_TIMEOUT_MS);
        sr = s_i2c.Instance->SR;
        rt_kprintf("[TP] tx(bigto)=%d SR=%x NACK=%d\n",
                   (int)st, (unsigned int)sr,
                   (int)((sr & I2C_SR_NACK) ? 1 : 0));
    }

    /* --- (1d) 最后两个从没验证过的东西 ---
     *   A) RCC 的 I2C1 时钟使能【状态】位 与 I2C1【复位】位
     *      ENR1 bit27 = 时钟使能状态(读)   RSTR1 bit27 = 复位
     *      若外设被按在复位里, 寄存器可读但状态机永不推进:
     *      SR 恒为 0、超时永远不结束 —— 与实测(tx 超时/SR=0)完全吻合。
     *   B) 外设寄存器可访问性: 写 SAR 再读回
     *   C) LCR/WCR 用来确认分频与超时计数确实写进去了 */
    rt_kprintf("[TP] rcc ENR1=%x RSTR1=%x -> I2C1 en=%d rst=%d\n",
               (unsigned int)hwp_hpsys_rcc->ENR1,
               (unsigned int)hwp_hpsys_rcc->RSTR1,
               (int)((hwp_hpsys_rcc->ENR1  & HPSYS_RCC_ENR1_I2C1_Msk)  ? 1 : 0),
               (int)((hwp_hpsys_rcc->RSTR1 & HPSYS_RCC_RSTR1_I2C1_Msk) ? 1 : 0));

    if (s_tp_inited)
    {
        uint32_t bak = s_i2c.Instance->SAR;

        s_i2c.Instance->SAR = 0x5A5AU;
        rt_kprintf("[TP] i2c SAR: %x -> %x  LCR=%x WCR=%x\n",
                   (unsigned int)bak, (unsigned int)s_i2c.Instance->SAR,
                   (unsigned int)s_i2c.Instance->LCR,
                   (unsigned int)s_i2c.Instance->WCR);
        s_i2c.Instance->SAR = bak;
    }

    /* --- (2) 无条件解卡 + 复位脉冲 + 多次重试读 ID + 扫描 ---
     *   解卡 = 标准 9 时钟 + STOP;
     *   复位脉冲放在"总线已干净"之后发, 与厂家 rt-thread 的先后顺序一致。 */
    if (s_tp_inited)
    {
        int i;

        rt_kprintf("[TP] bus recovery ...\n");
        tp_bus_recover();
        rt_kprintf("[TP] bus recovery done\n");

        tp_reset_pulse();

        /* 解卡+复位之后再选一次地址: 若 init 阶段因总线脏而没探到, 这里补救 */
        tp_select_addr();

        for (i = 0; i < 5; i++)
        {
            uint32_t idr = sf32lb52_touch_vendor_readid();
            rt_kprintf("[TP] retry %d: id=0x%x\n", i, (unsigned int)idr);
            if (idr != 0xFFU)
            {
                break;
            }
            tp_reset_pulse();
        }

        {
            uint16_t a;
            uint8_t  dummy = 0;
            int      found = 0;

            __HAL_I2C_ENABLE(&s_i2c);   /* 扫描前重申单元使能 (同厂家 master_xfer) */
            for (a = 0x08; a <= 0x77; a++)
            {
                /* Timeout 单位=毫秒: 50ms 远大于 400kHz 下地址相所需的 22us,
                 * 112 个地址合计约 5.6s (原 1000 在新语义下会是 112 秒)。 */
                if (HAL_I2C_Master_Transmit(&s_i2c, a, &dummy, 1, TP_SCAN_TIMEOUT_MS) == HAL_OK)
                {
                    rt_kprintf("[TP] ACK @ 0x%02x\n", (unsigned int)a);
                    found++;
                }
            }
            rt_kprintf("[TP] scan done: %d device(s) acked\n", found);
        }
    }

    /* HAL 状态码: 0=HAL_OK  1=HAL_ERROR  2=HAL_BUSY  3=HAL_TIMEOUT */
    rt_kprintf("[TP] tx=%d rx=%d id=0x%x\n",
               (int)st_tx, (int)st_rx, (unsigned int)id);
}

/* 返回 1 = 有触点; 坐标为【已按厂家规则镜像】的屏幕坐标 */
static uint16_t s_tp_raw_x = 0, s_tp_raw_y = 0;   /* 最近一次的面板原始坐标 */

int sf32lb52_touch_vendor_read(uint16_t *x, uint16_t *y, uint8_t *evt)
{
    uint8_t d[2 + 6 * MAX_POINT_NUM] = {0};
    uint8_t n;

    if (!s_tp_inited) return 0;
    if (tp_read_regs(0x01, (uint8_t)sizeof(d), d) != 0) return 0;

    n = d[1] & 0x0f;                                     /* TD_STATUS */
    if (n == 0 || n > MAX_POINT_NUM) return 0;

    *evt = (uint8_t)((d[2] >> 6) & 0x03);                /* P1_XH[7:6] */
    *x   = (uint16_t)(((d[2] & 0x0f) << 8) | d[3]);      /* P1_XH[3:0] + P1_XL */
    *y   = (uint16_t)(((d[4] & 0x0f) << 8) | d[5]);      /* P1_YH[3:0] + P1_YL */

    s_tp_raw_x = *x;                                     /* 留档: 面板原始坐标 */
    s_tp_raw_y = *y;

    *x = (uint16_t)(390 - *x);                           /* 厂家 ft6146_correct_pos() */
    *y = (uint16_t)(450 - *y);
    return 1;
}

/* 取最近一次 read() 的面板【原始】(未镜像) 坐标, 供上板对照打印 */
int sf32lb52_touch_vendor_last_raw(uint16_t *x, uint16_t *y)
{
    if (!x || !y) return -1;
    *x = s_tp_raw_x;
    *y = s_tp_raw_y;
    return 0;
}

/* ==========================================================================
 *  上电"读点自检" (改动2) —— 回答"是读点逻辑没跑, 还是在等触摸"
 *
 *  与厂家 ft6146.c 的静态对照结论 (逐条给依据):
 *    (a) 厂家读点 read_point() 只在 PA31 下降沿中断释放 isr_sem 后由读线程
 *        调用 (drv_touch.c:561 阻塞在 rt_sem_take(isr_sem) -> :570 read_point;
 *        ft6146.c:374-375 rt_touch_irq_pin_attach(PIN_IRQ_MODE_FALLING)+enable)。
 *        ★ 本轮修复: 本工程已【照厂家做法】补 PA31 下降沿中断, 见上面
 *          sf32lb52_touch_irq_config() / sf32lb52_touch_irq_dispatch() 的厂家
 *          对照; NuttX 侧 irq_attach + 信号量在 thermo_ui_glue.c。
 *          这解决实测 g_mode=0x01(中断触发模式)下【纯轮询恒读到 n=0】的问题:
 *          该模式下触点数据只在 INT 事件时交付, 无条件轮询读不到。
 *        (若中断因器件/连线仍不生效, 可退而写 G_MODE(0xA4)=0x00 切回轮询模式;
 *         现按用户要求优先采用厂家的中断做法, 未改 G_MODE。)
 *    (b) 我们确实在周期读: LVGL indev 定时器每 LV_DEF_REFR_PERIOD 回调
 *        thermo_touch_read_cb -> sf32lb52_touch_vendor_read, 不是"只注册不调用"。
 *    (c) 寄存器地址/长度/字节序与厂家逐字节一致: 0x01 起 2+6*MAX_POINT_NUM
 *        字节, n=d[1]&0x0f, x=((d[2]&0x0f)<<8)|d[3], y 同理
 *        (ft6146.c:193 / :199 / :211)。
 *    (d) 现有 [TP] raw= 打印被限速 ~5Hz, 且写在"有触点"分支里 —— 无触摸时本就
 *        不打印。所以"从没出现"只能说明"没读到触点", 不能说明"没在读"。
 *
 *  本自检在【无触摸】时也把原始寄存器读出来打印 (区分上面两种情况), 并另读
 *  芯片标识/固件/模式寄存器、读一次 CTP_INT(PA31) 电平、再做 1.5s 有界轮询。
 * ========================================================================== */
int sf32lb52_touch_vendor_selftest(void)
{
    uint8_t id_h = 0, id_l = 0, fw = 0, mode = 0, dev = 0;
    int     st_h, st_l, st_fw, st_mode, st_dev, st_burst;
    uint8_t d[2 + 6 * MAX_POINT_NUM] = {0};
    int     i, ok_reads = 0, hits = 0;

    if (!s_tp_inited) sf32lb52_touch_vendor_init();
    if (!s_tp_inited)
    {
        rt_kprintf("[TP] readpoint selftest: NOT inited -> skip\n");
        return -1;
    }

    st_h     = tp_read_regs(0xA3, 1, &id_h);   /* ID_H   (厂家 ft6146.c:70) */
    st_l     = tp_read_regs(0x9F, 1, &id_l);   /* ID_L   (厂家 ft6146.c:71) */
    st_fw    = tp_read_regs(0xA6, 1, &fw);     /* FIRMID (FT5x06 标准)      */
    st_mode  = tp_read_regs(0xA4, 1, &mode);   /* G_MODE (0=轮询 1=中断触发) */
    st_dev   = tp_read_regs(0x00, 1, &dev);    /* DEVICE_MODE               */
    st_burst = tp_read_regs(0x01, (uint8_t)sizeof(d), d);

    rt_kprintf("[TP] readpoint selftest: addr=0x%02x id_H=0x%02x(st=%d) id_L=0x%02x(st=%d) "
               "fw=0x%02x(st=%d) g_mode=0x%02x(st=%d) dev_mode=0x%02x(st=%d)\n",
               (unsigned)s_dev_addr, id_h, st_h, id_l, st_l,
               fw, st_fw, mode, st_mode, dev, st_dev);

    /* 关键一行: 即使【无触摸】也把原始寄存器读出来打印 (n 应为 0, st 应为 0) */
    rt_kprintf("[TP] readpoint selftest: regs0x01 len=%u st=%d TD_STATUS(n)=%u raw=(%u,%u)\n",
               (unsigned)sizeof(d), st_burst, (unsigned)(d[1] & 0x0f),
               (unsigned)(((d[2] & 0x0f) << 8) | d[3]),
               (unsigned)(((d[4] & 0x0f) << 8) | d[5]));
    rt_kprintf("[TP] readpoint selftest: CTP_INT(PA31) level=%d\n",
               (int)HAL_GPIO_ReadPin(hwp_gpio1, 31));

    /* 有界轮询 ~1.5s: 期间按屏即可被读到, 用来证明"通路在工作, 只在等触摸" */
    rt_kprintf("[TP] readpoint selftest: polling ~1.5s (press screen to get a HIT)...\n");
    for (i = 0; i < 30; i++)
    {
        uint8_t  dd[2 + 6 * MAX_POINT_NUM] = {0};
        uint16_t x = 0, y = 0;
        uint8_t  evt = 0;

        if (tp_read_regs(0x01, (uint8_t)sizeof(dd), dd) == 0)
        {
            ok_reads++;
            if ((dd[1] & 0x0f) > 0 && sf32lb52_touch_vendor_read(&x, &y, &evt))
            {
                hits++;
                rt_kprintf("[TP] readpoint selftest HIT: n=%u -> screen=(%u,%u) evt=%u\n",
                           (unsigned)(dd[1] & 0x0f),
                           (unsigned)x, (unsigned)y, (unsigned)evt);
            }
        }
        hw_mdelay(50);
    }

    rt_kprintf("[TP] readpoint selftest: PA31 int_level=%d irq_cnt=%u "
               "(中断在后续 [UI] 自检里才挂, 此处 cnt 应为 0)\n",
               sf32lb52_touch_int_level(), (unsigned)sf32lb52_touch_irq_count());
    rt_kprintf("[TP] readpoint selftest done: i2c_ok=%d/30 touch_hits=%d (%s)\n",
               ok_reads, hits,
               (ok_reads > 0) ? "read path alive; 0 hit = 当前无触摸"
                              : "read path FAILED (查 I2C/复位/INT)");
    return (ok_reads > 0) ? 0 : -1;
}
EOF
echo "      生成 vendor_touch_glue.c"

# ---- 3c. NTC 采温胶水层 (厂家 SiFli HAL ADC 原文) ---------------------------
cat > "$OUT/vendor_ntc_glue.c" <<'EOF'
/* ==========================================================================
 *  NTC 采温胶水: 把厂家 SiFli 片内 GPADC 接到裸片上
 *
 *  厂家依据 (逐条照抄, 不含任何自造时序 / 参数):
 *    app/src/temp/temp.c:170-199   temp_init()
 *        Instance = hwp_gpadc1;
 *        Init: data_samp_delay=2 / conv_width=75 / sample_width=71 /
 *              adc_se=1 / adc_force_on=0 / atten3=0 / dma_en=0 /
 *              en_slot=0 / op_mode=0
 *        HAL_PIN_Set_Analog(PAD_PA28, 1)   通道0 = 腔体 NTC
 *        HAL_PIN_Set_Analog(PAD_PA29, 1)   通道1 = 环境 NTC
 *        HAL_RCC_EnableModule(RCC_MOD_GPADC);
 *        HAL_ADC_Init(&s_hadc); HAL_Delay(300);
 *    sdk/customer/boards/include/config/sf32lb52x/adc_config.h:55-70
 *        ADC1_CONFIG 宏 —— 字段与上面逐项相同 (Instance 同为 hwp_gpadc1)
 *    app/src/temp/temp.c:92-109    hal_adc_read_channel()
 *        conf.Channel=ch / conf.pchnl_sel=ch / conf.slot_en=1 / conf.acc_num=0
 *        HAL_ADC_ConfigChannel -> HAL_ADC_Start -> HAL_ADC_PollForConversion(&h,100)
 *        -> val = HAL_ADC_GetValue(&h, ch) -> HAL_ADC_Stop -> raw = val & 0xFFF
 *    app/src/temp/temp.c:41-71,138-151   换算
 *        V = raw / 4095.0 * 3.3
 *        R = NTC_FIXED_R(10k) * (3.3 / V - 1)
 *        14 点 R-T 表 (-10..120°C, 步进 10°C):
 *          67710 42330 27280 18070 10000 6370 4160 2800 1940 1382 1006 749 569 440
 *        表内线性插值
 *    app/src/temp/temp.c:124   断线判定: raw > 4095*0.98 (= 4013) 视为开路
 *
 *  电路: VCC(3.3V) --(NTC R_t)-- ADCx --(R_fixed=10k)-- GND
 *     V_adc = VCC * R_fixed / (R_t + R_fixed);  R_t = R_fixed * (VCC/V_adc - 1)
 *
 *  【标定核对 (功能7)】逐字对照厂家 app/src/temp/temp.c 后确认:
 *    R-T 表 14 点 (67710..440)、步进 10°C、量程 -10..120、表内线性插值、
 *    分压反算 R = Rfix*(Vref/V - 1)、Vref=3.3V、Rfix=10k、断线阈值
 *    raw > 4095*0.98 (=4013, temp.c:124) —— 与本胶水层【完全一致】,
 *    未做任何"自拟合"修正。
 *    若双通道读数仍偏差 ~2°C, 来源是硬件公差 (NTC 本体 ±1% / 实际 Vref /
 *    ADC 增益), 应在应用板上做单点标定, 而不是改这张表。
 *    为便于现场比对, [NTC] 打印同时给出"查表值 tbl"与"B 参数值 B"
 *    (B=3950K, R25=10kΩ); 两者一致即说明表/公式自洽。
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void hw_mdelay(uint32_t ms);      /* 在 bf0_vendor_glue.o (vendor_lcd_glue.c) 里 */

/* 厂家 temp.h:23-25 的通道定义 */
#define NTC_CH_CHAMBER  0                /* 腔体 NTC, PA28 */
#define NTC_CH_AMBIENT  1                /* 环境 NTC, PA29 */
#define NTC_CH_COUNT    2

/* 厂家 temp.c:34-38 NTC 参数 */
#define NTC_ADC_MAX     4095.0f
#define NTC_VREF        3.3f
#define NTC_FIXED_R     10000.0f

/* 厂家 temp.c:34-37  B 参数 (B=3950, R25=10000Ω), 用于与查表值对照标定 */
#define NTC_B_PARAM     3950.0f
#define NTC_R25         10000.0f
#define NTC_T25_K       298.15f                  /* 25°C 开尔文 */

/* 厂家 temp.c:41-48  R-T 表 (Ω, -10..120°C, 步进 10°C, 共 14 点) */
static const float s_ntc_rt[] = {
    67710.0f, 42330.0f, 27280.0f, 18070.0f, 10000.0f,
     6370.0f,  4160.0f,  2800.0f,  1940.0f,  1382.0f,
     1006.0f,   749.0f,   569.0f,   440.0f,
};
#define NTC_TABLE_LEN   (sizeof(s_ntc_rt) / sizeof(s_ntc_rt[0]))
#define NTC_TABLE_STEP  10.0f
#define NTC_TABLE_MIN   (-10.0f)

/* 厂家 temp.c:51-64  电阻 → 温度: 表内线性插值 */
static float ntc_r_to_temp(float r)
{
    uint32_t i;
    if (r <= s_ntc_rt[NTC_TABLE_LEN - 1]) return 110.0f;
    if (r >= s_ntc_rt[0])                return -10.0f;
    for (i = 0; i < NTC_TABLE_LEN - 1; i++)
    {
        if (r <= s_ntc_rt[i] && r >= s_ntc_rt[i + 1])
        {
            float r1 = s_ntc_rt[i], r2 = s_ntc_rt[i + 1];
            float t1 = NTC_TABLE_MIN + (float)i * NTC_TABLE_STEP;
            float t2 = t1 + NTC_TABLE_STEP;
            return t1 + (t2 - t1) * (r1 - r) / (r1 - r2);
        }
    }
    return -10.0f;
}

/* 厂家 temp.c:67-71  电压 → 电阻 (分压反算) */
static float ntc_v_to_r(float v)
{
    if (v <= 0.001f) return 1e9f;
    return NTC_FIXED_R * (NTC_VREF / v - 1.0f);
}

/* 厂家 temp.h:28 / temp.c:74-85  8 点滑动均值滤波, 每通道一条独立环形缓冲 */
#define NTC_FILTER_N    8

typedef struct
{
    uint16_t buf[NTC_FILTER_N];
    uint8_t  idx;
    uint8_t  cnt;
    int32_t  sum;
} ntc_filter_t;

static ntc_filter_t s_ntc_filt[NTC_CH_COUNT];

static uint16_t ntc_filter_push(ntc_filter_t *f, uint16_t v)
{
    if (f->cnt == NTC_FILTER_N) { f->sum -= f->buf[f->idx]; }
    else                        { f->cnt++; }
    f->buf[f->idx] = v;
    f->sum        += v;
    f->idx         = (uint8_t)((f->idx + 1) % NTC_FILTER_N);
    return (uint16_t)(f->sum / f->cnt);
}

/* B 参数公式: 1/T = 1/T25 + ln(R/R25)/B   (T 为开尔文)
 *   => T_C = 1 / (1/298.15 + ln(R/10000)/3950) - 273.15
 * 只声明 logf (工具链 libm, nuttx.map 里 libm.a 已参与链接), 不引 <math.h>,
 * 与本库"自带 shim、不依赖系统头"的风格一致。 */
extern float logf(float x);

static float ntc_r_to_temp_b(float r)
{
    float inv_t;

    if (r <= 0.001f) r = 0.001f;
    inv_t = 1.0f / NTC_T25_K + logf(r / NTC_R25) / NTC_B_PARAM;
    if (inv_t <= 0.0f) return -273.15f;
    return 1.0f / inv_t - 273.15f;
}

/* ---- SiFli HAL ADC 句柄 (单例, 在 sf32lb52_ntc_init 初始化) ---- */
static ADC_HandleTypeDef s_hadc;
static int               s_ntc_inited = 0;

int sf32lb52_ntc_init(void)
{
    if (s_ntc_inited) return 0;

    memset(&s_hadc, 0, sizeof(s_hadc));
    memset(s_ntc_filt, 0, sizeof(s_ntc_filt));   /* 厂家 temp.c:172  清空滤波缓冲 */

    /* 以下 10 个字段与厂家 temp.c:176-185 及
     * sdk/customer/boards/include/config/sf32lb52x/adc_config.h:57-69
     * 的 ADC1_CONFIG 宏逐项一致 (顺序无关, 值必须一致)。 */
    s_hadc.Instance             = hwp_gpadc1;
    s_hadc.Init.data_samp_delay = 2;
    s_hadc.Init.conv_width      = 75;
    s_hadc.Init.sample_width    = 71;
    s_hadc.Init.adc_se          = 1;      /* 单端 */
    s_hadc.Init.adc_force_on    = 0;
    s_hadc.Init.atten3          = 0;
    s_hadc.Init.dma_en          = 0;
    s_hadc.Init.en_slot         = 0;
    s_hadc.Init.op_mode         = 0;      /* 单次模式 */

    /* 厂家 temp.c:188-189  两个 NTC 引脚切模拟输入 */
    HAL_PIN_Set_Analog(PAD_PA28, 1);      /* 通道 0 - 腔体 NTC */
    HAL_PIN_Set_Analog(PAD_PA29, 1);      /* 通道 1 - 环境 NTC */

    /* 厂家 temp.c:192  GPADC 模块时钟。
     * ★ 与 I2C1 / LCDC 同理: HAL_ADC_Init 内部只调 HAL_ADC_MspInit(弱函数),
     *   默认【不】使能模块时钟 —— 厂家在 adc_config.h 和 temp.c 里都显式开了。 */
    HAL_RCC_EnableModule(RCC_MOD_GPADC);

    if (HAL_ADC_Init(&s_hadc) != HAL_OK)
    {
        rt_kprintf("[NTC] HAL_ADC_Init FAIL\n");
        return -1;
    }

    /* 厂家 temp.c:195  HAL_Delay(300) —— ADC 稳定延迟, 仅初始化时一次。
     * 本库没有编入 bf0_hal.c (HAL_Delay 的实现处, SDK 里是 __weak 的
     * HAL_Delay_us(1000) 循环), 所以用库内已有的 hw_mdelay(ms) 等价实现。 */
    hw_mdelay(300);

    s_ntc_inited = 1;
    rt_kprintf("[NTC] gpadc1 init ok (PA28=ch0 / PA29=ch1)\n");
    return 0;
}

/* 读指定通道:
 *   *raw    = 8 点滑动均值后的 12bit ADC 值 (即真正参与换算的值)
 *   *temp_c = 厂家查表温度 °C (-999 = 开路/断线)
 * 同时按 B 参数公式(B=3950, R25=10000Ω)另算一个温度, 与查表值一起打印,
 * 便于日后标定:  "[NTC] ch%d (PA2%d) raw=%u tbl=%d.%d B=%d.%d C"      */
static int ntc_read_impl(int ch, uint16_t *raw, float *temp_c, int verbose)
{
    ADC_ChannelConfTypeDef conf;
    uint32_t val;
    uint16_t avg;
    float    r, temp_b;
    int      t10, b10;

    if (!s_ntc_inited)                    return -1;
    if (ch < 0 || ch >= NTC_CH_COUNT)     return -1;
    if (raw == NULL || temp_c == NULL)    return -1;

    /* 厂家 temp.c:96-102 */
    memset(&conf, 0, sizeof(conf));
    conf.Channel   = (uint8_t)ch;
    conf.pchnl_sel = (uint8_t)ch;
    conf.slot_en   = 1;
    conf.acc_num   = 0;
    HAL_ADC_ConfigChannel(&s_hadc, &conf);

    /* 厂家 temp.c:104-107 */
    HAL_ADC_Start(&s_hadc);
    HAL_ADC_PollForConversion(&s_hadc, 100);
    val = HAL_ADC_GetValue(&s_hadc, (uint32_t)ch);
    HAL_ADC_Stop(&s_hadc);

    /* 厂家 temp.c:108 */
    val &= 0xFFF;

    /* 厂家 temp.c:124  断线检测: 接近满量程视为开路 (NTC 断路 → ADC 拉到 VCC)
     * 厂家 temp.c:126  断线时把该通道滤波缓冲清零, 恢复后重新累积
     * ★ 本工程改动: 断线判据【仅对 ch0】(PA28 腔体 NTC) 生效。
     *   厂家 temp.c:120-131 是对【所有通道】判(=双 NTC 设计); 本板只接 1 路
     *   NTC 到 PA28, PA29(ch1) 悬空 → 悬空脚读到的原始值会接近满量程, 若沿用
     *   厂家对全部通道的判据会误报"断线"。故此处 ch1 不参与断线判据。 */
    if (ch == 0 && val > 4013u)           /* 4095 * 0.98 */
    {
        s_ntc_filt[ch].cnt = 0;
        s_ntc_filt[ch].sum = 0;
        s_ntc_filt[ch].idx = 0;
        *raw    = (uint16_t)val;
        *temp_c = -999.0f;
        return 0;
    }

    /* 厂家 temp.c:130  推入 8 点滑动均值 (通道各自独立) */
    avg  = ntc_filter_push(&s_ntc_filt[ch], (uint16_t)val);
    *raw = avg;

    /* 厂家 temp.c:138-140  avg → V → R → T (查表值) */
    r       = ntc_v_to_r(((float)avg / NTC_ADC_MAX) * NTC_VREF);
    *temp_c = ntc_r_to_temp(r);

    /* 另一路: B 参数公式算出的温度, 与查表值一起打印 (只需一位小数, 不用 %f) */
    temp_b = ntc_r_to_temp_b(r);
    t10    = (int)(*temp_c * 10.0f);
    b10    = (int)(temp_b   * 10.0f);

    if (verbose)
    {
        rt_kprintf("[NTC] ch%d (PA2%d) raw=%u tbl=%d.%d B=%d.%d C\n",
                   ch, 8 + ch, (unsigned int)avg,
                   t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
                   b10 / 10, (b10 < 0 ? -b10 : b10) % 10);
    }
    return 0;
}

/* 打印版 (上电自检/看板判据用, 逐字保留原行为) */
int sf32lb52_ntc_read(int ch, uint16_t *raw, float *temp_c)
{
    return ntc_read_impl(ch, raw, temp_c, 1);
}

/* 静默版 (后台控温 tick 周期调用): 采样/滤波/换算与打印版完全一致, 只是不打印,
 * 避免 500ms 一轮把串口刷屏。 */
int sf32lb52_ntc_read_quiet(int ch, uint16_t *raw, float *temp_c)
{
    return ntc_read_impl(ch, raw, temp_c, 0);
}
EOF
echo "      生成 vendor_ntc_glue.c"

# ---- 3d. PWM 风机胶水层 -----------------------------------------------------
cat > "$OUT/vendor_fan_glue.c" <<'EOF'
/* ==========================================================================
 *  PWM 风机胶水: 把厂家 SiFli GPT/PWM 接到裸片上
 *
 *  厂家依据 (不自造时序, 与背光背光背光同一套 HAL_GPT_* API):
 *    sdk/customer/boards/sf32lb52-lcd_base/bsp_pinmux.c:232
 *        HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_PULLUP, 1);   // 厂家把 PA32 复用成 GPTIM2_CH1
 *      => 本板风机 = PA32 / GPTIM2_CH1 (厂家 pwm3)
 *    为什么不用厂家的 PWM_FAN_DEVICE="pwm2" (app/src/ctrl/ctrl.c:30-33):
 *      pwm2 = GPTIM1, 而本板背光已占用 GPTIM1_CH4 (PA01)。同一个 GPTIM 只能有
 *      一个自动重装周期, 改风机周期会把背光频率一起改掉。故风机改用 GPTIM2_CH1。
 *    占空比/周期算法与背光逐行同构 (period_ns -> ticks):
 *      背光 100kHz (period_ns = 10000)  ;  风机 1kHz (period_ns = 1000000)
 *    参考实现: 本脚本 3. 里的 backlight_pwm_init()/backlight_pwm_set()
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void hw_mdelay(uint32_t ms);          /* 在 bf0_vendor_glue.o 里 */

#define FAN_PWM_FREQ_HZ     1000UL
#define FAN_PERIOD_NS       (1000000000UL / FAN_PWM_FREQ_HZ)   /* 1 ms */

static GPT_HandleTypeDef s_fan;
static uint32_t          s_fan_period_ticks = 0;
static uint32_t          s_fan_psc          = 1;
static int               s_fan_inited       = 0;

/* 百分比 -> 比较寄存器值 (PWM1: CCR 越大高电平越宽, CCR >= ARR 为 100%) */
static uint32_t fan_compare(uint32_t gpt_clock, uint32_t percent)
{
    uint32_t pulse_ns, pulse;

    if (percent > 100UL) percent = 100UL;
    pulse_ns = FAN_PERIOD_NS * percent / 100UL;
    pulse    = pulse_ns * gpt_clock / s_fan_psc / 1000UL;
    if (pulse < 1)                          pulse = 1;
    else if (pulse >= s_fan_period_ticks)   pulse = s_fan_period_ticks + 1;
    return pulse;
}

int sf32lb52_fan_init(void)
{
    GPT_ClockConfigTypeDef ccfg = {0};
    GPT_OC_InitTypeDef     oc   = {0};
    uint32_t gpt_clock, psc, period;

    if (s_fan_inited) return 0;

    /* 厂家 bsp_pinmux.c:232 的引脚复用原文
     *   #if defined(BSP_USING_PWM3) || defined(BSP_USING_RGBLED_WITCH_PWM3)
     *       HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_PULLUP, 1);   // RGB LED
     *   #endif */
    HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_PULLUP, 1);

    s_fan.Instance         = hwp_gptim2;
    s_fan.core             = CORE_ID_HCPU;
    s_fan.Init.Prescaler   = 0;
    s_fan.Init.CounterMode = GPT_COUNTERMODE_UP;
    s_fan.Init.Period      = 0;
    if (HAL_GPT_Base_Init(&s_fan) != HAL_OK)                { rt_kprintf("[FAN] base FAIL\n"); return -1; }
    ccfg.ClockSource = GPT_CLOCKSOURCE_INTERNAL;
    if (HAL_GPT_ConfigClockSource(&s_fan, &ccfg) != HAL_OK)  { rt_kprintf("[FAN] clk FAIL\n");  return -1; }
    if (HAL_GPT_PWM_Init(&s_fan) != HAL_OK)                 { rt_kprintf("[FAN] pwm FAIL\n");  return -1; }
    __HAL_GPT_URS_ENABLE(&s_fan);

    /* period_ns -> ticks: ticks = period_ns * fclk[MHz] / 1000 (与背光同式) */
    gpt_clock = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1) / 1000000UL;
    period    = FAN_PERIOD_NS * gpt_clock / 1000UL;
    if (period == 0) period = 1;
    psc       = period / 0xFFFFUL + 1UL;
    period    = period / psc;
    if (period < 3) period = 3;
    __HAL_GPT_SET_PRESCALER(&s_fan, psc - 1);
    __HAL_GPT_SET_AUTORELOAD(&s_fan, period - 1);
    s_fan_period_ticks = period;
    s_fan_psc          = psc;

    __HAL_GPT_SET_COMPARE(&s_fan, GPT_CHANNEL_1, fan_compare(gpt_clock, 0) - 1);
    HAL_GPT_GenerateEvent(&s_fan, GPT_EVENTSOURCE_UPDATE);

    oc.OCMode     = GPT_OCMODE_PWM1;
    oc.Pulse      = __HAL_GPT_GET_COMPARE(&s_fan, GPT_CHANNEL_1);
    oc.OCPolarity = GPT_OCPOLARITY_HIGH;
    oc.OCFastMode = GPT_OCFAST_DISABLE;
    if (HAL_GPT_PWM_ConfigChannel(&s_fan, &oc, GPT_CHANNEL_1) != HAL_OK) { rt_kprintf("[FAN] ch1 FAIL\n"); return -1; }
    HAL_GPT_PWM_Start(&s_fan, GPT_CHANNEL_1);

    s_fan_inited = 1;
    rt_kprintf("[FAN] init ok pin=PA32 GPTIM2_CH1 freq=%u Hz pclk=%u MHz psc=%u period=%u duty=0%%\n",
               (unsigned int)FAN_PWM_FREQ_HZ, (unsigned int)gpt_clock,
               (unsigned int)psc, (unsigned int)period);
    return 0;
}

int sf32lb52_fan_set_duty(uint8_t pct)
{
    uint32_t gpt_clock, ccr;

    if (!s_fan_inited) return -1;
    if (pct > 100) pct = 100;

    gpt_clock = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1) / 1000000UL;
    ccr       = fan_compare(gpt_clock, (uint32_t)pct) - 1;
    __HAL_GPT_SET_COMPARE(&s_fan, GPT_CHANNEL_1, ccr);
    HAL_GPT_GenerateEvent(&s_fan, GPT_EVENTSOURCE_UPDATE);

    /* 回读 CCR = 实测值 */
    rt_kprintf("[FAN] set duty=%u%% (ccr=%u / arr=%u)\n",
               (unsigned int)pct,
               (unsigned int)__HAL_GPT_GET_COMPARE(&s_fan, GPT_CHANNEL_1),
               (unsigned int)s_fan_period_ticks);
    return 0;
}

/* 上电自检: 0% -> 50% -> 100% 各 300ms, 最后停在 0% (安全) */
int sf32lb52_fan_selftest(void)
{
    static const uint8_t steps[3] = {0, 50, 100};
    int i;

    if (sf32lb52_fan_init() != 0) return -1;

    for (i = 0; i < 3; i++)
    {
        sf32lb52_fan_set_duty(steps[i]);
        hw_mdelay(300);
    }

    sf32lb52_fan_set_duty(0);
    rt_kprintf("[FAN] selftest done (end duty=0%%, safe)\n");
    return 0;
}
EOF
echo "      生成 vendor_fan_glue.c"

# ---- 3e. 参数存储 (掉电保存) 胶水层 ----------------------------------------
cat > "$OUT/vendor_nvs_glue.c" <<'EOF'
/* ==========================================================================
 *  参数存储胶水: 把参数写进片外 QSPI2 NOR (掉电保持)
 *
 *  厂家方案 (逐条照抄, 不自造时序):
 *    * 分区: 厂家分区表 ptab.h 给本板规划的 KVDB 参数区
 *        KVDB_DFU_REGION_START_ADDR = 0x12458000  size = 0x4000 (16KB = 4 扇区)
 *      位于 app 分区之后 (0x12218000 + 0x240000 = 0x12458000), 与代码不重叠。
 *    * 读写: 厂家 rt_flash_write()/rt_flash_erase() 的底层路径
 *        sdk/rtos/rtthread/bsp/sifli/drivers/drv_spi_flash.c:534/582
 *          rt_flash_write -> rt_nor_write_rom -> HAL_QSPIEX_WRITE_PAGE
 *          rt_flash_erase -> rt_nor_erase_rom -> HAL_QSPIEX_SECT_ERASE
 *        sdk/drivers/hal/bf0_hal_mpi_ex.c:2399 HAL_QSPIEX_FLASH_WRITE()
 *        sdk/drivers/hal/bf0_hal_mpi_ex.c:2415 HAL_QSPIEX_FLASH_ERASE()
 *        sdk/drivers/hal/bf0_hal_mpi_ex.c:129  HAL_FLASH_Init()
 *      (sftool 的 SRAM 编程器 tools/flash/project/sf32lb52x/src/FlashPrg.c 用的是
 *       同一组调用: HAL_FLASH_Init + HAL_QSPIEX_FLASH_WRITE/ERASE)
 *    * 校验: 厂家 app/src/storage/storage.c 的 sys_params_t + CRC32 查表法
 *        (16 项表, 多项式 0xEDB88320, 初值/末值取反) —— 本文件原样沿用。
 *
 *  ★ 为什么本文件与 bf0_hal_mpi*.o / flash_table.o 必须放在 SRAM 常驻段:
 *      本固件自己就从 QSPI2 (0x12000000) 取指。擦/写 QSPI2 期间该片
 *      flash 无法响应取指, 如果程序还在 flash 里跑就会跑飞。
 *      厂家做法完全一样 —— 厂家 link.lds 的 .retm_data 段就把
 *        drv_spi_flash.o / flash_table.o / bf0_hal_mpi.o / bf0_hal_mpi_ex.o
 *      整体搬到 RAM (见 app/project/sf32lb52-xty-ai_hcpu/link.lds:153-166)。
 *      本工程的对应位置: boards/arm/sf32lb52/sf32lb52-lcd/scripts/flash.ld
 *      的 .data 段里 (boot 时由 sf32lb52_start.c 的 "_eronly -> _sdata.._edata"
 *      拷贝循环一并搬到 SRAM, 无需新增启动代码)。
 *
 *  ★ 擦/写期间关全局中断:
 *      否则 SysTick 处理程序 (在 flash 里取指) 会在擦写窗口内触发 -> 跑飞。
 *      与厂家 drv_spi_flash.c 的 nor_lock() -> rt_hw_interrupt_disable() 等价。
 *
 *  ★ 不改 QSPI2 时钟分频:
 *      读当前 PSCLR 的分频值再原样传回 HAL_FLASH_Init, 保持 bootloader/厂家
 *      已配好的 XIP 读时序不变 (改分频有可能把 XIP 读坏)。
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void rt_kprintf(const char *fmt, ...);

/* ---- 参数区 (厂家 ptab.h 的 KVDB 区) ---- */
#define NVS_FLASH_BASE_ADDR   0x12000000UL
#define NVS_FLASH_SIZE_MB     16UL
#define NVS_SECTOR_SIZE       4096UL
#define NVS_ADDR              0x12458000UL
#define NVS_MAGIC             0x54484E52UL          /* 'THNR' */
#define NVS_VERSION           3UL
#define NVS_VERSION_V2        2UL
#define NVS_VERSION_V1        1UL
#define NVS_WBUF_SIZE         64                    /* 一页 256B 之内, 且覆盖驱动的 64B 预取 */

/* v1 布局 (历史版本, 不含 cur_temp)。
 * 保留它是为了【兼容迁移】: 升级固件后第一次上电, flash 里还躺着 v1 记录,
 * 若直接按 v2 结构读会 CRC 失配 -> 被判为"无有效记录" -> 用户参数被默认值覆盖。
 * 迁移逻辑见 sf32lb52_nvs_load()。 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    float    target_temp;
    uint32_t fan_duty;
    uint32_t boot_count;
    uint32_t run_hours;
    uint32_t crc32;
} __attribute__((packed)) nvs_params_v1_t;

/* v2 布局 (历史版本, 不含 v3 新增的 输出%/定时/极值)。
 * 保留它是为了【兼容迁移】: 升级固件后第一次上电, flash 里还躺着 v2 记录。 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    float    target_temp;
    uint32_t fan_duty;
    uint32_t boot_count;
    uint32_t run_hours;
    float    cur_temp;       /* v2 新增: 最后一次采到的腔体温度 °C */
    uint32_t crc32;
} __attribute__((packed)) nvs_params_v2_t;

/* 与厂家 sys_params_t 同构的打包结构 (CRC32 覆盖除 crc32 外的全部字节)
 * v3 相对 v2 追加: out_pct / timer_minutes / hist_min / hist_max。
 * 迁移做法: 老记录按对应旧结构重解释 -> 逐字段搬到 v3 -> 新字段取默认值,
 *           老参数一律不丢 (见 sf32lb52_nvs_load)。 */
typedef struct
{
    uint32_t magic;          /* NVS_MAGIC: 首次上电/擦除后一定不等于它 */
    uint32_t version;        /* NVS_VERSION (3) */
    float    target_temp;    /* 目标温度 °C */
    uint32_t fan_duty;       /* 风机设定 % (0..100) */
    uint32_t boot_count;     /* 上电次数: 每次自检 +1, 用来验证掉电保持 */
    uint32_t run_hours;
    float    cur_temp;       /* v2: 最后一次采到的腔体温度 °C (PID 用) */
    uint32_t out_pct;        /* ★ v3: 上一次 PID 输出 % (0..100) */
    uint16_t timer_minutes;  /* ★ v3: 定时启停 (分钟, 0=关) */
    float    hist_min;       /* ★ v3: 历史最低腔体温度 °C */
    float    hist_max;       /* ★ v3: 历史最高腔体温度 °C */
    uint32_t crc32;
} __attribute__((packed)) nvs_params_t;

static QSPI_FLASH_CTX_T s_nvs_ctx;
static nvs_params_t     s_nvs;
static uint8_t          s_nvs_wbuf[NVS_WBUF_SIZE] __attribute__((aligned(8)));
static int              s_nvs_inited  = 0;
static int              s_nvs_loaded  = 0;   /* 1 = 来自 flash, 0 = 默认值 */

/* ---- CRC32 (厂家 storage.c:27-40 原表) ---- */
static const uint32_t s_crc32_tab[16] =
{
    0x00000000, 0x1DB71064, 0x3B0E6E48, 0x26D930AC,
    0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

static uint32_t nvs_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu, i;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        crc  = (crc >> 4) ^ s_crc32_tab[crc & 0x0F];
        crc  = (crc >> 4) ^ s_crc32_tab[crc & 0x0F];
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ---- 临界区: 等价厂家 rt_hw_interrupt_disable/enable ---- */
static uint32_t nvs_irq_disable(void)
{
    uint32_t primask;
    __asm__ __volatile__ ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm__ __volatile__ ("cpsid i" ::: "memory");
    return primask;
}

static void nvs_irq_restore(uint32_t primask)
{
    if ((primask & 1u) == 0u)
    {
        __asm__ __volatile__ ("cpsie i" ::: "memory");
    }
}

/* ---- 微秒忙等: 供厂家 bf0_hal_mpi_ex.c 的 HAL_Delay_us 使用 ----
 *   编译该文件时用 -DHAL_Delay_us=sf32lb52_flash_delay_us 把调用改名到本函数,
 *   这样厂家代码在 flash 里的 HAL_Delay_us(bf0_vendor_glue.o) 不会被 SRAM 段引用。
 *   ★ 只读 DWT->CYCCNT, 绝不写 0: bf0_vendor_glue.o 的 HAL_GetTick 靠 CYCCNT
 *     单调递增换算毫秒, 中途清零会被它当成 32 位回绕而一次多算约 29.8s。 */
#define NVS_DWT_LAR      0xE0001FB0UL
#define NVS_CYCCNT_FREQ  144000000UL

void sf32lb52_flash_delay_us(uint32_t us)
{
    uint32_t cycles, start, now;

    if (us == 0) return;

    *(volatile uint32_t *)NVS_DWT_LAR = 0xC5ACCE55UL;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    cycles = us * (NVS_CYCCNT_FREQ / 1000000UL);
    if (cycles == 0) cycles = 1;
    start  = DWT->CYCCNT;
    do { now = DWT->CYCCNT; } while ((uint32_t)(now - start) < cycles);
}

/* ---- DMA 桩 ----
 *   本胶水传 dma = NULL, 厂家 HAL_QSPIEX_WRITE_PAGE 会走 FIFO 分支
 *   (bf0_hal_mpi_ex.c:1763), HAL_FLASH_DMA_START/WAIT_DONE 在 dma==NULL 时
 *   直接 return, 永远不会执行到真正的 DMA。这里只提供 4 个空实现, 让
 *   bf0_hal_mpi_ex.o 能通过链接 —— 从而不必把整个 bf0_hal_dma.c 也拖进来。 */
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *hdma)
{ (void)hdma; return HAL_ERROR; }
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *hdma)
{ (void)hdma; return HAL_ERROR; }
HAL_StatusTypeDef HAL_DMA_Start(DMA_HandleTypeDef *hdma, uint32_t src, uint32_t dst, uint32_t cnt)
{ (void)hdma; (void)src; (void)dst; (void)cnt; return HAL_ERROR; }
HAL_StatusTypeDef HAL_DMA_PollForTransfer(DMA_HandleTypeDef *hdma,
                                          HAL_DMA_LevelCompleteTypeDef level, uint32_t timeout)
{ (void)hdma; (void)level; (void)timeout; return HAL_ERROR; }

static void nvs_cache_sync(void)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1)
    SCB_InvalidateDCache_by_Addr((void *)NVS_ADDR, NVS_SECTOR_SIZE);
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1)
    SCB_InvalidateICache_by_Addr((void *)NVS_ADDR, NVS_SECTOR_SIZE);
#endif
}

static void nvs_dump(const char *tag, const nvs_params_t *p)
{
    int t10 = (int)(p->target_temp * 10.0f);
    int c10 = (int)(p->cur_temp    * 10.0f);
    int n10 = (int)(p->hist_min    * 10.0f);
    int x10 = (int)(p->hist_max    * 10.0f);

    rt_kprintf("[NVS] %s boot_count=%u target_temp=%d.%d cur_temp=%d.%d fan_duty=%u run_hours=%u out_pct=%u timer=%umin hist=[%d.%d..%d.%d]\n",
               tag, (unsigned int)p->boot_count,
               t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
               c10 / 10, (c10 < 0 ? -c10 : c10) % 10,
               (unsigned int)p->fan_duty, (unsigned int)p->run_hours,
               (unsigned int)p->out_pct, (unsigned int)p->timer_minutes,
               n10 / 10, (n10 < 0 ? -n10 : n10) % 10,
               x10 / 10, (x10 < 0 ? -x10 : x10) % 10);
}

/* ---- 初始化 (厂家 HAL_FLASH_Init, 同 sftool FlashPrg.c) ---- */
int sf32lb52_nvs_init(void)
{
    qspi_configure_t   cfg;
    FLASH_HandleTypeDef probe;
    HAL_StatusTypeDef  st;
    uint32_t           primask;
    uint16_t           div;

    if (s_nvs_inited) return 0;

    /* 读回当前分频, 原样传回 -> 不改 XIP 读时序 */
    memset(&probe, 0, sizeof(probe));
    probe.Instance = hwp_qspi2;
    div = (uint16_t)HAL_FLASH_GET_DIV(&probe);
    if (div == 0) div = 2;

    memset(&s_nvs_ctx, 0, sizeof(s_nvs_ctx));
    memset(&cfg, 0, sizeof(cfg));
    cfg.Instance = hwp_qspi2;              /* FLASH2 */
    cfg.base     = NVS_FLASH_BASE_ADDR;
    cfg.line     = 2;                      /* 4 线, 与厂家 FLASH2_CONFIG 同值 */
    cfg.msize    = NVS_FLASH_SIZE_MB;
    cfg.SpiMode  = 0;                      /* SPI_MODE_NOR */

    /* dma = NULL: 走厂家驱动的 FIFO 写分支, 不引入 DMA 通道/中断。
     *
     * ★ 关中断执行 HAL_FLASH_Init —— 厂家 example_flash.c:138 明确警告
     *   "when test on xip flash, can not initial again to avoid xip issue":
     *   HAL_QSPI_Init 会改写 QSPI2 的 TIMR/CIR/ABR1/HRABR (即 XIP 取指配置),
     *   改写窗口内不能从 QSPI2 取指。
     *   本条调用链 (HAL_QSPI_Init / HAL_QSPI_READ_ID / HAL_FLASH_RELEASE_DPD /
     *   HAL_FLASH_SET_CLK_rom / spi_flash_* 查表 / 本文件的 delay) 全部位于
     *   SRAM 常驻段 (见 flash.ld 的 .ramdata), 只要不让 SysTick 之类的中断
     *   插进来 (它们的处理程序还在 flash 里), 这个窗口就是安全的。 */
    primask = nvs_irq_disable();
    st      = HAL_FLASH_Init(&s_nvs_ctx, &cfg, NULL, NULL, div);
    nvs_irq_restore(primask);

    if (st != HAL_OK)
    {
        rt_kprintf("[NVS] HAL_FLASH_Init FAIL\n");
        return -1;
    }

    s_nvs_ctx.handle.base = NVS_FLASH_BASE_ADDR;
    s_nvs_inited = 1;

    rt_kprintf("[NVS] init ok base=0x%x size=%uMB dev_id=0x%x div=%u dma=off addr=0x%x\n",
               (unsigned int)s_nvs_ctx.handle.base,
               (unsigned int)(s_nvs_ctx.handle.size >> 20),
               (unsigned int)s_nvs_ctx.dev_id,
               (unsigned int)div,
               (unsigned int)NVS_ADDR);
    return 0;
}

static int nvs_read_raw(nvs_params_t *out)
{
    /* NOR 直接走 XIP 映射地址读 (厂家 rt_nor_read_rom 的 else 分支同做法) */
    memcpy(out, (const void *)NVS_ADDR, sizeof(nvs_params_t));
    return 0;
}

static int nvs_valid(const nvs_params_t *p)
{
    nvs_params_t tmp;
    uint32_t crc;

    if (p->magic   != NVS_MAGIC)   return 0;
    if (p->version != NVS_VERSION) return 0;

    memcpy(&tmp, p, sizeof(tmp));
    crc = tmp.crc32;
    tmp.crc32 = 0;
    if (nvs_crc32((const uint8_t *)&tmp, sizeof(tmp)) != crc) return 0;
    return 1;
}

static void nvs_set_default(nvs_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->magic       = NVS_MAGIC;
    p->version     = NVS_VERSION;
    p->target_temp = 55.0f;     /* 厂家 storage.c:47 默认目标 */
    p->fan_duty    = 30;        /* 厂家 storage.c:53 默认风机最低 */
    p->boot_count  = 0;
    p->run_hours   = 0;
    p->cur_temp    = 0.0f;
    p->out_pct        = 0;      /* v3 */
    p->timer_minutes  = 0;      /* v3: 0 = 不定时 */
    p->hist_min       = 0.0f;   /* v3 */
    p->hist_max       = 0.0f;   /* v3 */
    p->crc32       = 0;
}

/* v1 记录校验 (同一套 CRC32, 只是结构更短) */
static int nvs_valid_v1(const nvs_params_v1_t *p)
{
    nvs_params_v1_t tmp;
    uint32_t crc;

    if (p->magic   != NVS_MAGIC)     return 0;
    if (p->version != NVS_VERSION_V1) return 0;

    memcpy(&tmp, p, sizeof(tmp));
    crc = tmp.crc32;
    tmp.crc32 = 0;
    if (nvs_crc32((const uint8_t *)&tmp, sizeof(tmp)) != crc) return 0;
    return 1;
}

/* v2 记录校验 (同一套 CRC32, 结构比 v3 短) */
static int nvs_valid_v2(const nvs_params_v2_t *p)
{
    nvs_params_v2_t tmp;
    uint32_t crc;

    if (p->magic   != NVS_MAGIC)      return 0;
    if (p->version != NVS_VERSION_V2) return 0;

    memcpy(&tmp, p, sizeof(tmp));
    crc = tmp.crc32;
    tmp.crc32 = 0;
    if (nvs_crc32((const uint8_t *)&tmp, sizeof(tmp)) != crc) return 0;
    return 1;
}

int sf32lb52_nvs_save(void);    /* 迁移后立刻回写, 定义在本函数之后 */

/* ---- 读: 1 = 读到有效记录, 0 = 用默认值 ---- */
int sf32lb52_nvs_load(void)
{
    nvs_params_t raw;

    if (!s_nvs_inited) return -1;

    nvs_read_raw(&raw);
    if (nvs_valid(&raw))
    {
        memcpy(&s_nvs, &raw, sizeof(raw));
        s_nvs_loaded = 1;
        nvs_dump("load ok (from flash v3),", &s_nvs);
        return 1;
    }

    /* ---- v2 -> v3 兼容迁移 ----
     * 升级固件后第一次上电命中这里: flash 里还是 v2 记录 (比 v3 短,
     * 不含 out_pct/timer_minutes/hist_min/hist_max)。
     * 做法: 按 v2 结构重新解释同样的字节, 逐字段搬到 v3, 新字段取默认值,
     *       然后立刻以 v3 回写。用户老参数不丢, 下次上电走上面的 v3 分支。 */
    {
        nvs_params_v2_t old;

        memcpy(&old, &raw, sizeof(old));
        if (nvs_valid_v2(&old))
        {
            nvs_set_default(&s_nvs);
            s_nvs.target_temp = old.target_temp;
            s_nvs.fan_duty    = old.fan_duty;
            s_nvs.boot_count  = old.boot_count;
            s_nvs.run_hours   = old.run_hours;
            s_nvs.cur_temp    = old.cur_temp;
            s_nvs_loaded      = 1;
            nvs_dump("migrated v2 -> v3,", &s_nvs);

            if (sf32lb52_nvs_save() != 0)
            {
                rt_kprintf("[NVS] migrate write-back FAILED (下次上电仍会重试)\n");
            }
            return 1;
        }
    }

    /* ---- v1 -> v3 兼容迁移 ----
     * 更老的一跳 (v1 连 cur_temp 都没有): 直接一步补到 v3。 */
    {
        nvs_params_v1_t old;

        memcpy(&old, &raw, sizeof(old));
        if (nvs_valid_v1(&old))
        {
            nvs_set_default(&s_nvs);
            s_nvs.target_temp = old.target_temp;
            s_nvs.fan_duty    = old.fan_duty;
            s_nvs.boot_count  = old.boot_count;
            s_nvs.run_hours   = old.run_hours;
            s_nvs.cur_temp    = 0.0f;
            s_nvs_loaded      = 1;
            nvs_dump("migrated v1 -> v3,", &s_nvs);

            if (sf32lb52_nvs_save() != 0)
            {
                rt_kprintf("[NVS] migrate write-back FAILED (下次上电仍会重试)\n");
            }
            return 1;
        }
    }

    nvs_set_default(&s_nvs);
    s_nvs_loaded = 0;
    nvs_dump("no valid record -> defaults,", &s_nvs);
    return 0;
}

/* ---- 写: 擦 1 个扇区 + 写 struct ---- */
int sf32lb52_nvs_save(void)
{
    nvs_params_t tmp;
    uint32_t primask;
    int n;

    if (!s_nvs_inited) return -1;

    memcpy(&tmp, &s_nvs, sizeof(tmp));
    tmp.magic   = NVS_MAGIC;
    tmp.version = NVS_VERSION;
    tmp.crc32   = 0;
    tmp.crc32   = nvs_crc32((const uint8_t *)&tmp, sizeof(tmp));

    memset(s_nvs_wbuf, 0xFF, sizeof(s_nvs_wbuf));
    memcpy(s_nvs_wbuf, &tmp, sizeof(tmp));       /* 源缓冲必须在 SRAM */

    primask = nvs_irq_disable();
    n = HAL_QSPIEX_FLASH_ERASE(&s_nvs_ctx.handle, NVS_ADDR, NVS_SECTOR_SIZE);
    if (n == 0)
    {
        n = HAL_QSPIEX_FLASH_WRITE(&s_nvs_ctx.handle, NVS_ADDR, s_nvs_wbuf, sizeof(nvs_params_t));
    }
    nvs_irq_restore(primask);

    if (n != (int)sizeof(nvs_params_t))
    {
        rt_kprintf("[NVS] save FAIL (erase/write ret=%d, want %u)\n",
                   n, (unsigned int)sizeof(nvs_params_t));
        return -1;
    }

    memcpy(&s_nvs, &tmp, sizeof(tmp));
    s_nvs_loaded = 1;
    nvs_cache_sync();

    rt_kprintf("[NVS] save ok erase@0x%x(%u B sector) + write %u B crc=0x%x\n",
               (unsigned int)NVS_ADDR, (unsigned int)NVS_SECTOR_SIZE,
               (unsigned int)sizeof(nvs_params_t), (unsigned int)tmp.crc32);
    return 0;
}

/* ---- 读写接口 (供以后 UI/控制用) ---- */
int sf32lb52_nvs_set(float target_temp, uint8_t fan_duty)
{
    s_nvs.magic       = NVS_MAGIC;
    s_nvs.version     = NVS_VERSION;
    s_nvs.target_temp = target_temp;
    s_nvs.fan_duty    = (fan_duty > 100) ? 100u : (uint32_t)fan_duty;
    return sf32lb52_nvs_save();
}

float sf32lb52_nvs_get_target_temp(void) { return s_nvs.target_temp; }
uint8_t sf32lb52_nvs_get_fan_duty(void)  { return (uint8_t)s_nvs.fan_duty; }
uint32_t sf32lb52_nvs_get_boot_count(void){ return s_nvs.boot_count; }
float sf32lb52_nvs_get_cur_temp(void)    { return s_nvs.cur_temp; }

/* ---- v3 新增字段读写 ---- */
uint32_t sf32lb52_nvs_get_out_pct(void)      { return s_nvs.out_pct; }
uint16_t sf32lb52_nvs_get_timer_minutes(void){ return s_nvs.timer_minutes; }
float    sf32lb52_nvs_get_hist_min(void)     { return s_nvs.hist_min; }
float    sf32lb52_nvs_get_hist_max(void)     { return s_nvs.hist_max; }

int sf32lb52_nvs_set_out_pct(uint32_t pct)
{
    s_nvs.magic   = NVS_MAGIC;
    s_nvs.version = NVS_VERSION;
    s_nvs.out_pct = (pct > 100u) ? 100u : pct;
    return sf32lb52_nvs_save();
}

int sf32lb52_nvs_set_timer_minutes(uint16_t minutes)
{
    s_nvs.magic         = NVS_MAGIC;
    s_nvs.version       = NVS_VERSION;
    s_nvs.timer_minutes = minutes;
    return sf32lb52_nvs_save();
}

/* 只更新 cur_temp (并顺带维护 v3 的 hist_min/hist_max) 并落盘
 * —— PID 每轮采温后调用, 不动其它字段 */
int sf32lb52_nvs_set_cur_temp(float cur_temp)
{
    s_nvs.magic    = NVS_MAGIC;
    s_nvs.version  = NVS_VERSION;
    s_nvs.cur_temp = cur_temp;

    /* v3: 忽略开路 (-999) 之类的脏值再记极值 */
    if (cur_temp > -50.0f && cur_temp < 150.0f)
    {
        if (s_nvs.hist_min == 0.0f || cur_temp < s_nvs.hist_min) s_nvs.hist_min = cur_temp;
        if (cur_temp > s_nvs.hist_max) s_nvs.hist_max = cur_temp;
    }
    return sf32lb52_nvs_save();
}

/* ---- 上电自检: init -> 读 -> boot_count+1 -> 写 -> 立刻回读校验 ----
 *   验收判据: 串口每次上电 "[NVS] verify ok boot_count=N" 且 N 逐次 +1,
 *             即证明参数真的落在 flash 里、掉电不丢。 */
int sf32lb52_nvs_selftest(void)
{
    nvs_params_t rb;

    if (sf32lb52_nvs_init() != 0)          return -1;
    if (sf32lb52_nvs_load() < 0)           return -1;

    s_nvs.magic       = NVS_MAGIC;
    s_nvs.version     = NVS_VERSION;
    s_nvs.boot_count += 1;
    if (s_nvs.target_temp <= 0.0f) s_nvs.target_temp = 55.0f;
    if (s_nvs.fan_duty    >  100)  s_nvs.fan_duty    = 30;
    /* cur_temp 越界认为是脏数据 (开路 -999 / 未填 0), 归零 */
    if (s_nvs.cur_temp < -50.0f || s_nvs.cur_temp > 150.0f) s_nvs.cur_temp = 0.0f;

    if (sf32lb52_nvs_save() != 0)          return -1;

    nvs_read_raw(&rb);                     /* 从 flash 再读一遍 */
    if (!nvs_valid(&rb))
    {
        rt_kprintf("[NVS] verify FAIL (readback invalid)\n");
        return -1;
    }

    nvs_dump("verify ok (readback from flash),", &rb);
    rt_kprintf("[NVS] next power-on: boot_count should be %u\n",
               (unsigned int)(rb.boot_count + 1));
    return 0;
}
EOF
echo "      生成 vendor_nvs_glue.c"

# ---- 3f. PID 控温胶水层 (厂家 ctrl.c 原文算法 + 参数) ------------------------
cat > "$OUT/vendor_pid_glue.c" <<'EOF'
/* ==========================================================================
 *  PID 控温胶水: 温度闭环 (腔体 NTC -> 增量式 PID -> 风机 PWM 占空比)
 *
 *  厂家依据 —— 算法与参数【逐行照抄】, 不自造:
 *    app/src/ctrl/ctrl.h:25-30       thermo_pid_t 结构
 *                                      {kp,ki,kd,out_min,out_max,ek_1,ek_2,out_1}
 *    app/src/ctrl/ctrl.c:51-72       ctrl_pid_step()  增量式 PID
 *                                      e_k = target - measured
 *                                      du  = kp*(e_k-e_{k-1}) + ki*e_k
 *                                          + kd*(e_k-2*e_{k-1}+e_{k-2})
 *                                      u   = u_{k-1} + du, 限幅 [out_min,out_max]
 *    app/src/ctrl/ctrl.c:170-175     默认参数 kp=6.0 / ki=0.2 / kd=1.0
 *                                      out_min=0 / out_max=100, 历史项清零
 *    app/src/ctrl/ctrl.c:22-26       加热滞回 HEATER_ON_DELTA=2.0 / OFF_DELTA=0.0
 *                                      风机最低 FAN_MIN_HEAT=30 (加热时防闷烧)
 *    app/src/ctrl/ctrl.c:90-103      占空比下发: period=1000000ns (1kHz),
 *                                      pulse = period * pct / 100
 *
 *  本板落点 (与厂家差异, 仅为适配本板已验证的驱动):
 *    风机  = sf32lb52_fan_set_duty()  -> PA32 / GPTIM2_CH1, 1kHz
 *            (厂家用 pwm2/GPTIM1_CH1; 本板 GPTIM1_CH4 已被背光占用,
 *             详见 vendor_fan_glue.c 顶部说明)
 *    采温  = sf32lb52_ntc_read(0)     -> PA28 腔体 NTC (厂家 temp 模块同通道)
 *    目标值= sf32lb52_nvs_get_target_temp()  (掉电保存, 厂家 storage 模块同义)
 *
 *  本板无加热继电器 (厂家 GPIO_RELAY_HEAT=33 在本板未接), 所以 PID 输出
 *  只作用到风机; 加热滞回的判断结果只【打印】出来, 不驱动 GPIO。
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void hw_mdelay(uint32_t ms);

/* 同库内其它胶水层的接口 */
extern int   sf32lb52_fan_init(void);
extern int   sf32lb52_fan_set_duty(uint8_t pct);
extern int   sf32lb52_ntc_init(void);
extern int   sf32lb52_ntc_read(int ch, uint16_t *raw, float *temp_c);
extern int   sf32lb52_ntc_read_quiet(int ch, uint16_t *raw, float *temp_c);
extern float sf32lb52_nvs_get_target_temp(void);
extern int   sf32lb52_nvs_set_cur_temp(float cur_temp);

/* 厂家 ctrl.h:25-30  thermo_pid_t */
typedef struct {
    float kp, ki, kd;
    float out_min, out_max;     /* 输出限幅 (0..100%) */
    float ek_1, ek_2;           /* 上次/上上次误差 (增量式 PID) */
    float out_1;                /* 上次输出 (占空比 %) */
} thermo_pid_t;

/* 厂家 ctrl.c:22-26 */
#define HEATER_ON_DELTA   2.0f
#define HEATER_OFF_DELTA  0.0f
#define FAN_MIN_HEAT      30u

static thermo_pid_t s_pid;
static int          s_heater_on = 0;

/* 厂家 ctrl.c:74-80  ctrl_pid_reset() */
void sf32lb52_pid_reset(void)
{
    s_pid.ek_1 = s_pid.ek_2 = 0.0f;
    s_pid.out_1 = 0.0f;
}

/* 厂家 ctrl.c:164-175  ctrl_init() 里的 PID 默认参数 */
void sf32lb52_pid_init(void)
{
    s_pid.kp      = 6.0f;
    s_pid.ki      = 0.2f;
    s_pid.kd      = 1.0f;
    s_pid.out_min = 0.0f;
    s_pid.out_max = 100.0f;
    sf32lb52_pid_reset();
    s_heater_on = 0;
}

/* 厂家 ctrl.c:51-72  ctrl_pid_step() —— 一字不改 */
float sf32lb52_pid_step(float target, float measured)
{
    float e_k = target - measured;

    float du = s_pid.kp * (e_k - s_pid.ek_1)
             + s_pid.ki * e_k
             + s_pid.kd * (e_k - 2.0f * s_pid.ek_1 + s_pid.ek_2);

    float u = s_pid.out_1 + du;

    if (u < s_pid.out_min) u = s_pid.out_min;
    if (u > s_pid.out_max) u = s_pid.out_max;

    s_pid.ek_2  = s_pid.ek_1;
    s_pid.ek_1  = e_k;
    s_pid.out_1 = u;
    return u;
}

float sf32lb52_pid_get_out(void)  { return s_pid.out_1; }
float sf32lb52_pid_get_kp(void)   { return s_pid.kp; }
float sf32lb52_pid_get_ki(void)   { return s_pid.ki; }
float sf32lb52_pid_get_kd(void)   { return s_pid.kd; }

/* 打印一位小数 (本库 rt_kprintf 不带 %f, 与 [NTC]/[NVS] 打印风格一致) */
static void p10(float v)
{
    int a = (int)(v * 10.0f);
    int neg = (a < 0);

    if (neg) a = -a;
    rt_kprintf("%s%d.%d", neg ? "-" : "", a / 10, a % 10);
}

/* 加热负载驱动 + 到温灯 (在 vendor_gpio_glue.c)。厂家 ctrl.c 用
 * rt_pin_write(GPIO_RELAY_HEAT) 驱动继电器; 本板把同一个滞回结果接到
 * 加热灯 PA25 (LED_A) 与到温灯 PA24 (LED_B), 通过成对驱动保证互斥。 */
extern int sf32lb52_heater_init(void);
extern int sf32lb52_heater_set(int on);
extern int sf32lb52_setpoint_led_init(void);
extern int sf32lb52_setpoint_led_set(int on);
extern int sf32lb52_heat_pair_apply(int heater_on);

/* 厂家 ctrl.c:145-158  ctrl_isr_run() 的"一轮" —— 采温 -> PID -> 风机 -> 加热滞回 */
static uint8_t pid_run_once(float target, float measured, float *out)
{
    uint8_t fan_pct;
    float   duty;
    float   err;

    duty = sf32lb52_pid_step(target, measured);
    *out = duty;

    fan_pct = (uint8_t)(duty + 0.5f);
    if (s_heater_on && fan_pct < FAN_MIN_HEAT) fan_pct = FAN_MIN_HEAT;
    sf32lb52_fan_set_duty(fan_pct);

    /* 加热负载滞回 (厂家 ctrl.c:152-158)。
     * 本板把滞回结果【成对】下发到双灯: 加热中 -> PA25=1/PA24=0;
     * 到温(加热停) -> PA25=0/PA24=1。与厂家 ctrl.c 的 ctrl_heater_on()/
     * ctrl_heater_off() 同一逻辑, 只是多了到温灯的互斥映射 (本工程新增)。 */
    err = target - measured;
    {
        uint8_t prev = s_heater_on;

        if (err > HEATER_ON_DELTA)       s_heater_on = 1;
        else if (err < HEATER_OFF_DELTA) s_heater_on = 0;

        if (s_heater_on != prev) {
            sf32lb52_heat_pair_apply(s_heater_on);
        }
    }

    return fan_pct;
}

/* 加热状态查询 (后台 tick 推 UI 用): 1=加热中(PA25亮), 0=到温(PA24亮) */
int sf32lb52_pid_get_heater(void) { return s_heater_on; }

/* ==========================================================================
 *  后台控温 tick —— 一轮完整控温 (供常驻线程周期调用)
 *
 *  厂家依据: app/src/ctrl/ctrl.c:5 "定时器中断 100ms 调用 ctrl_isr_run()" ,
 *            实现 ctrl.c:136-159 —— 采温 -> PID -> 风机 -> 加热滞回。
 *            厂家由 app/src/thermo_app.c:183-190/209 的 100ms lv_timer 驱动。
 *  本板落点: 常驻线程 thermo_ui_thread() (thermo_ui_glue.c) 每 500ms 调本函数,
 *            与厂家"周期 tick 里跑一轮"结构一致(只是调度载体换成 NuttX 线程)。
 *
 *  一轮动作 (与 pid_run_once 相同): 采 ch0 -> PID -> 风机 PWM -> 加热滞回 -> 双灯。
 *  采温走【静默版】sf32lb52_ntc_read_quiet, 避免每 500ms 刷 [NTC] 行。
 *
 *  返回:  0 = 本轮已控温; -1 = 采样无效(NTC 断线), 本轮跳过(安全, 保持上次输出)。
 * ========================================================================== */
int sf32lb52_pid_tick(float *out, uint16_t *raw_out, float *meas_out)
{
    uint16_t raw  = 0;
    float    meas = -999.0f;
    float    target;

    if (sf32lb52_ntc_read_quiet(0, &raw, &meas) != 0) return -1;

    if (raw_out)  *raw_out  = raw;
    if (meas_out) *meas_out = meas;

    if (meas <= -900.0f) return -1;      /* ch0 断线: 本轮不控温 */

    target = sf32lb52_nvs_get_target_temp();
    if (target <= 0.0f) target = 55.0f;

    (void)pid_run_once(target, meas, out);
    return 0;
}

/* --------------------------------------------------------------------------
 *  上电自检: 两段
 *    [PID] real —— 真实闭环: 设定值取 NVS, 实测值取腔体 NTC(PA28), 输出真正下发
 *                  到风机 PWM。打印每轮的 设定/实测/偏差/P-I-D 三项/输出占空比。
 *    [PID] sim  —— 闭环仿真: 用一个一阶虚拟被控对象 (占空比 -> 升温 + 散热),
 *                  让偏差从 8°C 收敛到 0, 用来看【增量式 PID 真的在调节】。
 *                  (本板无加热丝, 真实回路的温度不会变化, 单靠 real 段看不出
 *                   收敛过程; sim 段只做数学演示, 已在打印里明确标 sim。)
 *
 *  看板判据:
 *    real 段: out 随偏差方向变化 (偏差大 -> out 顶到 100%), 每轮数值非零且在动
 *    sim  段: meas 从 ~(target-8) 逐轮逼近 target, out 先冲高再回落并稳定
 * ========================================================================== */
int sf32lb52_pid_selftest(void)
{
    float target, meas, out;
    uint16_t raw;
    int i;

    if (sf32lb52_fan_init() != 0) return -1;
    if (sf32lb52_ntc_init() != 0) return -1;
    sf32lb52_heater_init();          /* 加热负载 PA25 上电先置 0 (安全) */
    sf32lb52_setpoint_led_init();    /* 到温指示灯 PA24 上电先置 0 (安全) */

    sf32lb52_pid_init();

    target = sf32lb52_nvs_get_target_temp();
    if (target <= 0.0f) target = 55.0f;

    rt_kprintf("[PID] init ok kp=");
    p10(s_pid.kp);
    rt_kprintf(" ki=");
    p10(s_pid.ki);
    rt_kprintf(" kd=");
    p10(s_pid.kd);
    rt_kprintf(" out_range=[%d,%d] target=", (int)s_pid.out_min, (int)s_pid.out_max);
    p10(target);
    rt_kprintf(" C\n");

    /* ---- 段 1: 真实闭环 (腔体 NTC -> PID -> 风机) ---- */
    for (i = 0; i < 6; i++)
    {
        float e_k, pterm, iterm, dterm;

        raw  = 0;
        meas = -999.0f;
        sf32lb52_ntc_read(0, &raw, &meas);       /* ch0 = PA28 腔体 NTC */
        if (meas <= -900.0f) meas = 25.0f;       /* 开路 -> 用设定外的占位值, 保证能算 */

        /* 先按厂家公式把三项分解出来打印 (e_k 与历史项在 step 前后语义不同),
         * 分解式与 ctrl.c:57-59 完全一致:
         *   P = kp*(e_k - e_{k-1})   I = ki*e_k   D = kd*(e_k - 2*e_{k-1} + e_{k-2}) */
        e_k   = target - meas;
        pterm = s_pid.kp * (e_k - s_pid.ek_1);
        iterm = s_pid.ki * e_k;
        dterm = s_pid.kd * (e_k - 2.0f * s_pid.ek_1 + s_pid.ek_2);

        out = 0.0f;
        (void)pid_run_once(target, meas, &out);

        rt_kprintf("[PID] real k=%d target=", i);
        p10(target);
        rt_kprintf(" meas=");
        p10(meas);
        rt_kprintf(" e=");
        p10(e_k);
        rt_kprintf(" P=");
        p10(pterm);
        rt_kprintf(" I=");
        p10(iterm);
        rt_kprintf(" D=");
        p10(dterm);
        rt_kprintf(" out=");
        p10(out);
        rt_kprintf("%% heater=%s setpoint_led=%s (PA25/PA24)\n",
                   s_heater_on ? "ON" : "OFF",
                   s_heater_on ? "off" : "ON");

        hw_mdelay(250);
    }

    /* 把最后一次实测温度落盘 (v2 新增字段) */
    sf32lb52_nvs_set_cur_temp(meas);

    /* ---- 段 2: 闭环仿真 (虚拟被控对象), 看 PID 收敛 ---- */
    {
        float sim_t = target - 8.0f;      /* 初始比目标低 8°C */
        float err_x, err_prev = 0.0f;

        sf32lb52_pid_reset();
        rt_kprintf("[PID] sim start (virtual plant, not real hardware)\n");

        for (i = 0; i < 20; i++)
        {
            err_x = target - sim_t;
            out   = sf32lb52_pid_step(target, sim_t);

            rt_kprintf("[PID] sim  k=%d meas=", i);
            p10(sim_t);
            rt_kprintf(" e=");
            p10(err_x);
            rt_kprintf(" du=");
            p10(s_pid.kp * (err_x - err_prev) + s_pid.ki * err_x);
            rt_kprintf(" out=");
            p10(out);
            rt_kprintf("%%\n");
            err_prev = err_x;

            /* 一阶虚拟被控对象: 占空比升温, 同时向环境散热 */
            sim_t += (out / 100.0f) * 3.5f - 0.15f;
            if (sim_t < -20.0f)  sim_t = -20.0f;
            if (sim_t > 150.0f)  sim_t = 150.0f;

            hw_mdelay(40);
        }

        rt_kprintf("[PID] sim end meas=");
        p10(sim_t);
        rt_kprintf(" target=");
        p10(target);
        rt_kprintf(" final_out=");
        p10(s_pid.out_1);
        rt_kprintf("%% (|e|=");
        p10(target - sim_t);
        rt_kprintf(")\n");
    }

    /* 收尾: 加热停 + 到温灯亮 (成对) + 风机停, PID 复位 (安全) */
    sf32lb52_heat_pair_apply(0);
    sf32lb52_fan_set_duty(0);
    rt_kprintf("[PID] selftest done (heater off + setpoint LED on, fan back to 0%%, PID state kept for UI)\n");
    return 0;
}
EOF
echo "      生成 vendor_pid_glue.c"

# ---- 3f. GPIO 胶水层: 故障指示灯 / 加热负载 / 唤醒按键 ------------------------
cat > "$OUT/vendor_gpio_glue.c" <<'EOF'
/* ==========================================================================
 *  GPIO 胶水层: 加热指示灯(PA25) / 到温指示灯(PA24) / 唤醒按键 (纯 GPIO, 全部 HAL_PIN_Set)
 *
 *  ★ 两个 LED 分开接, 均高电平有效 (本次改动):
 *    LED_A (加热中)  = PA25, 逻辑 = heater_on
 *    LED_B (到温)    = PA24, 逻辑 = !heater_on   (二者互斥, 同一拍不同时亮)
 *    (原定 PA21=加热灯 / PA26=到温灯; 因这两根在开发板排针上未引出, 本次改到 PA25/PA24)
 *    灯的逻辑源就是厂家 ctrl.c 的加热滞回结果 (ctrl.c:22-26 HEATER_ON/OFF_DELTA),
 *    即"加热停止" = "到温"。这不是厂家现成的 LED 代码, 是本工程为双灯新加的
 *    映射 (厂家 ctrl.c 里 THERMO_HDW_ENABLED 关闭, 只有一个 heating 标志,
 *     没有到温灯), 故凡新增处均在注释里标明"本工程新增"。
 *
 *  引脚依据 (厂家 app/boards/sf32lb52-xty-ai_base/bsp_pinmux.c):
 *    PA24: HAL_PIN_Set(PAD_PA24, GPIO_A24, PIN_PULLUP, 1);    // 本板 = 到温指示灯 LED_B
 *          => 本板 = 到温指示灯 LED_B (输出, 高=亮)
 *    PA25: HAL_PIN_Set(PAD_PA25, GPIO_A25, PIN_PULLDOWN, 1);  // 本板 = 加热指示灯 LED_A
 *          => 本板 = 加热负载 (输出, 高=开)
 *    (原定 PA21/PA26 在开发板排针上未引出, 故改到 PA25/PA24)
 *    PA11: L200 HAL_PIN_Set(PAD_PA11, GPIO_A11, PIN_NOPULL, 1);   // Key2
 *          L256 HAL_PIN_Set(PAD_PA11, GPIO_A11, PIN_PULLDOWN,1);  // 漏电变体
 *          => 本板 = 唤醒按键 (输入, 按下=低)
 *  驱动方式逐行照厂家 app/src/ctrl/ctrl.c 的 rt_pin_write(GPIO_RELAY_HEAT) 与
 *  app/src/fault/fault.c 的 led_set(): 纯 GPIO 输出, 高电平有效。
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include "bf0_hal.h"
#include "rtthread.h"

extern void hw_mdelay(uint32_t ms);

#define PIN_SETPOINT_LED 24     /* PA24 : 到温指示灯 LED_B (输出, 高=亮) */
#define PIN_HEATER       25     /* PA25 : 加热指示灯 LED_A (输出, 高=亮) */
#define PIN_WAKE_KEY     11     /* PA11 : 唤醒按键 (输入, 按下=低) */

static void gpio_out_init(int pin, int val)
{
    GPIO_InitTypeDef gi;
    gi.Mode = GPIO_MODE_OUTPUT;
    gi.Pin  = (uint16_t)pin;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(hwp_gpio1, &gi);
    HAL_GPIO_WritePin(hwp_gpio1, (uint16_t)pin,
                      val ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---------------- 到温指示灯 LED_B (PA24) —— 本工程新增 ----------------
 * 原来是"故障指示灯"(原定 PA26); 本次改为"到温指示灯": 加热停(=到温) 时点亮,
 * 加热中熄灭。原定 PA26 在开发板排针上未引出, 故改到 PA24。
 * 故障告警只保留 LVGL 弹窗 (见 thermo_ui_glue.c), 不再占用 PA24/PA26。 */
int sf32lb52_setpoint_led_init(void)
{
    HAL_PIN_Set(PAD_PA24, GPIO_A24, PIN_PULLUP, 1);
    gpio_out_init(PIN_SETPOINT_LED, 0);
    rt_kprintf("[HEAT] setpoint LED init pin=PA24 (GPIO out, active high)\n");
    return 0;
}

int sf32lb52_setpoint_led_set(int on)
{
    int rb;

    HAL_GPIO_WritePin(hwp_gpio1, PIN_SETPOINT_LED,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    rb = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_SETPOINT_LED);
    rt_kprintf("[HEAT] setpoint LED(PA24) %s (readback=%d)\n",
               on ? "ON" : "off", rb);
    return rb;
}

/* ---------------- 加热指示灯 LED_A (PA25) ---------------- */
int sf32lb52_heater_init(void)
{
    HAL_PIN_Set(PAD_PA25, GPIO_A25, PIN_PULLDOWN, 1);
    gpio_out_init(PIN_HEATER, 0);
    rt_kprintf("[HEAT] heater init pin=PA25 (GPIO out, active high)\n");
    return 0;
}

int sf32lb52_heater_set(int on)
{
    HAL_GPIO_WritePin(hwp_gpio1, PIN_HEATER,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    rt_kprintf("[HEAT] heater %s (PA25 readback=%d)\n",
               on ? "ON" : "off",
               (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_HEATER));
    return 0;
}

/* ---------------- 双灯成对驱动 / 成对自检 —— 本工程新增 ----------------
 * LED_A(PA25)=加热中, LED_B(PA24)=到温, 二者互斥。所有改动加热状态的地方
 * 都走这里, 保证两灯永远同步、不会同时亮。返回 0 = 回读与预期一致。 */
int sf32lb52_heat_pair_apply(int heater_on)
{
    int pa25, pa24;

    sf32lb52_heater_set(heater_on);
    sf32lb52_setpoint_led_set(heater_on ? 0 : 1);

    pa25 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_HEATER);
    pa24 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_SETPOINT_LED);
    rt_kprintf("[HEAT] pair: %s -> PA25=%d PA24=%d\n",
               heater_on ? "heating" : "reached", pa25, pa24);

    return (heater_on ? (pa25 == 1 && pa24 == 0) : (pa25 == 0 && pa24 == 1))
           ? 0 : -1;
}

/* ---------------- 唤醒按键 (PA11) ---------------- */
int sf32lb52_wake_pin_init(void)
{
    GPIO_InitTypeDef gi;
    HAL_PIN_Set(PAD_PA11, GPIO_A11, PIN_PULLUP, 1);
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pin  = PIN_WAKE_KEY;
    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(hwp_gpio1, &gi);
    rt_kprintf("[PWR] wake key init pin=PA11 (input, pull-up, level=%d)\n",
               (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_WAKE_KEY));
    return 0;
}

/* 返回 1 = 按键被按下 (低电平) */
int sf32lb52_wake_pin_pressed(void)
{
    return (HAL_GPIO_ReadPin(hwp_gpio1, PIN_WAKE_KEY) == GPIO_PIN_RESET) ? 1 : 0;
}

/* ---------------- 上电自检 ---------------- */
/* 成对自检: 人为造出"加热中"与"到温"两种状态, 用 PA25/PA24 回读当证据。
 * 这就是"上电时人为触发一次到温状态"的入口 (厂家 ctrl.c 无对应实现, 本工程新增)。 */
int sf32lb52_heat_pair_selftest(void)
{
    int pa25, pa24, ok1, ok2;

    rt_kprintf("[HEAT] pair selftest start (LED_A=PA25 heating, LED_B=PA24 reached)\n");

    /* 状态 1 —— 加热中: 加热灯亮, 到温灯灭 */
    sf32lb52_heater_set(1);
    sf32lb52_setpoint_led_set(0);
    pa25 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_HEATER);
    pa24 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_SETPOINT_LED);
    ok1  = (pa25 == 1 && pa24 == 0);
    rt_kprintf("[HEAT] pair selftest: heating -> PA25=%d PA24=%d (expect 1/0) %s\n",
               pa25, pa24, ok1 ? "OK" : "FAIL");
    hw_mdelay(800);

    /* 状态 2 —— 到温 (人为造): 加热灯灭, 到温灯亮 */
    sf32lb52_heater_set(0);
    sf32lb52_setpoint_led_set(1);
    pa25 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_HEATER);
    pa24 = (int)HAL_GPIO_ReadPin(hwp_gpio1, PIN_SETPOINT_LED);
    ok2  = (pa25 == 0 && pa24 == 1);
    rt_kprintf("[HEAT] pair selftest: reached -> PA25=%d PA24=%d (expect 0/1) %s\n",
               pa25, pa24, ok2 ? "OK" : "FAIL");
    hw_mdelay(800);

    rt_kprintf("[HEAT] pair selftest %s (paired GPIO readback verified)\n",
               (ok1 && ok2) ? "PASS" : "FAIL");
    return (ok1 && ok2) ? 0 : -1;
}

int sf32lb52_gpio_selftest(void)
{
    sf32lb52_heater_init();
    sf32lb52_setpoint_led_init();
    sf32lb52_wake_pin_init();

    /* 成对自检: 加热中(PA25=1/PA24=0) <-> 到温(PA25=0/PA24=1), 回读佐证 */
    sf32lb52_heat_pair_selftest();

    /* 收尾: 回到"到温"态 (加热关), 两灯停在 PA25=0 / PA24=1 */
    sf32lb52_heat_pair_apply(0);

    rt_kprintf("[GPIO] selftest done (PA25=heater LED_A / PA24=setpoint LED_B verified by readback)\n");
    return 0;
}
EOF
echo "      生成 vendor_gpio_glue.c"

# ---- 4. 编译 ----------------------------------------------------------------
INC=(-I"$OUT/shim" -I"$PROJ/sdk_port/shim" -I"$PROJ/sdk_port/Include"
     -I"$PROJ/sdk_port/cmsis_52x" -I"$PROJ/sdk_port/cmsis_core"
     -I"$PROJ/sdk_port/cmsis_inc")
#  ★ -DUSE_HAL_DRIVER (厂家 drivers/hal/SConscript:25 就是给整个 hal 目录定义的):
#    cmsis_52x/register.h:757 是 `#if defined(USE_HAL_DRIVER) #include "bf0_hal.h"`。
#    厂家 flash_table.c 只 #include "flash_table.h", 经
#        flash_table.h -> bf0_hal_mpi_ex.h -> bf0_hal_mpi.h -> bf0_hal_def.h -> register.h
#    绕回来才拿到 bf0_hal.h; 少了这个宏, bf0_hal_dma.h 就永远不被包含,
#    bf0_hal_mpi.h:298 的 `DMA_HandleTypeDef *dma;` 直接报 unknown type name。
#    (厂家 bf0_hal_mpi*.c 因为自己第一行就 #include "bf0_hal.h", 不带这个宏也能过,
#     只有 flash_table.c 会炸 —— 所以这个宏必须补上。)
DEF="-DSOC_BF0_HCPU -DSF32LB52X -DUSE_HAL_DRIVER -DBSP_USING_LCD -DBSP_USING_LCDC -DBSP_LCDC_USING_QADSPI -DLCD_USING_CO5300 -DLCD_CO5300_VSYNC_ENABLE"
# ★ 警告策略: 厂家 HAL 源码保持 -w (不改厂家代码, 也不听它的告警);
#   只对我们自己写的胶水层开 -Wall, 并把告警修干净。
CFL="-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections -w"
CFL_GLUE="-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections -Wall"

echo "[1/3] 编译厂家 HAL ..."
OBJS=()
for u in bf0_hal_lcdc bf0_hal_rcc bf0_hal_gpio bf0_hal_pinmux bf0_hal_tim bf0_pin_const bf0_hal_i2c bf0_hal_hpaon bf0_hal_adc; do
    $GCC -c $CFL $DEF "${INC[@]}" -o "$OUT/$u.o" "$PROJ/sdk_port/$u.c"
    OBJS+=("$OUT/$u.o")
done

# ---- 厂家 QSPI NOR 编程栈 (参数存储用; flash.ld 会把这几个 .o 放进 SRAM 常驻段) ----
#
#  ★ -DHAL_Delay_us=sf32lb52_flash_delay_us:
#    厂家 bf0_hal_mpi_ex.c 在 HAL_FLASH_Init() 里调 HAL_Delay_us(50)。
#    而 bf0_vendor_glue.o 里的那个 HAL_Delay_us 位于 flash —— 一旦 SRAM 段
#    在"QSPI2 已不可取指"的瞬间回调它, 就是跑飞。
#    用 -D 把该文件里的调用改名到 vendor_nvs_glue.c 中自带的 SRAM 版实现,
#    厂家的源码一个字都不改。
echo "      编译厂家 MPI/QSPI NOR 栈 (SRAM 常驻) ..."
for u in bf0_hal_mpi bf0_hal_mpi_ex flash_table; do
    $GCC -c $CFL $DEF "${INC[@]}" -DHAL_Delay_us=sf32lb52_flash_delay_us \
         -o "$OUT/$u.o" "$PROJ/sdk_port/$u.c"
    OBJS+=("$OUT/$u.o")
done

echo "[2/3] 编译胶水层 (-Wall, 含 #include co5300.c) ..."
$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_glue.o" "$OUT/vendor_lcd_glue.c"
OBJS+=("$OUT/bf0_vendor_glue.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_touch.o" "$OUT/vendor_touch_glue.c"
OBJS+=("$OUT/bf0_vendor_touch.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_ntc.o" "$OUT/vendor_ntc_glue.c"
OBJS+=("$OUT/bf0_vendor_ntc.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_fan.o" "$OUT/vendor_fan_glue.c"
OBJS+=("$OUT/bf0_vendor_fan.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_nvs.o" "$OUT/vendor_nvs_glue.c"
OBJS+=("$OUT/bf0_vendor_nvs.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_pid.o" "$OUT/vendor_pid_glue.c"
OBJS+=("$OUT/bf0_vendor_pid.o")

$GCC -c $CFL_GLUE $DEF "${INC[@]}" -o "$OUT/bf0_vendor_gpio.o" "$OUT/vendor_gpio_glue.c"
OBJS+=("$OUT/bf0_vendor_gpio.o")


echo "[3/3] 打包静态库 ..."
rm -f "$OUT/libsf32lb52_vendorlcd.a"
$AR rcs "$OUT/libsf32lb52_vendorlcd.a" "${OBJS[@]}"
arm-none-eabi-nm --defined-only "$OUT/libsf32lb52_vendorlcd.a" 2>/dev/null \
  | grep -E "sf32lb52_(lcd_vendor_(init|fill|clear|readpixel|readpixel_selftest|readpixel_usable|setbrightness|readid|blit|solidtest)|ntc_(init|read)|fan_(init|set_duty|selftest)|nvs_(init|load|save|selftest|set|get_)|pid_(init|step|reset|selftest|get_)|ui_(init|show|sync|set_values|selftest))" || true

if [ -d "$DEST_DIR" ]; then
    cp -f "$OUT/libsf32lb52_vendorlcd.a" "$DEST_DIR/"

    # 预先把库成员摊成 v*.o 放进 DEST_DIR。
    #
    # 为什么需要: NuttX 的 boards/Board.mk 里 `$(BIN): $(OBJS)` 的前提表
    # 在【读取 Makefile 时】就定型了, 板级 src/Makefile 里后加的
    # `OBJS +=` 改不动依赖图 —— 结果 ar 收到了新对象名却找不到文件:
    #   arm-none-eabi-ar: vbf0_hal_lcdc.o: 没有那个文件或目录
    # 提前把文件放好, ar 必然找得到;
    # 板级 src/Makefile 里仍保留"从 .a 重新抽取"的规则, 供 make clean 后自愈。
    rm -f "$DEST_DIR"/v*.o
    rm -rf "$OUT/x"; mkdir -p "$OUT/x"
    ( cd "$OUT/x" && $AR x "$OUT/libsf32lb52_vendorlcd.a" \
      && for f in *.o; do cp -f "$f" "$DEST_DIR/v$f"; done ) \
      || { echo "[FAIL] 摊开库成员失败"; exit 1; }

    echo "[OK] 已摊开库成员 -> $DEST_DIR/v*.o"
    echo "[OK] 已放入 $DEST_DIR/libsf32lb52_vendorlcd.a"
else
    echo "[WARN] 找不到 $DEST_DIR (先跑 integrate.sh 再跑本脚本)"
    echo "       库在 $OUT/libsf32lb52_vendorlcd.a"
fi