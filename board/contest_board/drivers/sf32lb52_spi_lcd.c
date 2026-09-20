/**
 * @file sf32lb52_spi_lcd.c
 * @brief SF32LB52 SPI + ST7789 LCD 驱动
 *
 * SPI1 基地址: 0x50095000
 * 引脚: PA15(SCK), PA16(MOSI), PA17(MISO)
 * LCD: PA18(CS), PA19(DC), PA20(RST), PA25(BL)
 */
#include <nuttx/config.h>

/* ---- SPI 寄存器定义 ---- */
typedef struct {
    volatile uint32_t TOP_CTRL;     /* 0x00 顶层控制 */
    volatile uint32_t FIFO_CTRL;    /* 0x04 FIFO控制 */
    volatile uint32_t INTE;         /* 0x08 中断使能 */
    volatile uint32_t TO;           /* 0x0C 超时 */
    volatile uint32_t DATA;         /* 0x10 数据 */
    volatile uint32_t STATUS;       /* 0x14 状态 */
    volatile uint32_t PSP_CTRL;     /* 0x18 */
    volatile uint32_t NW_CTRL;      /* 0x1C */
    volatile uint32_t NW_STATUS;    /* 0x20 */
    volatile uint32_t RWOT_CTRL;    /* 0x24 */
    volatile uint32_t RWOT_CCM;     /* 0x28 */
    volatile uint32_t RWOT_CVWRN;   /* 0x2C */
    volatile uint32_t RSVD2[3];
    volatile uint32_t CLK_CTRL;     /* 0x3C 时钟控制 */
} SPI_TypeDef;

/* ---- 基地址 ---- */
#define SPI1_BASE       0x50095000UL
#define SPI1            ((SPI_TypeDef *)SPI1_BASE)

/* ---- TOP_CTRL 位定义 ---- */
#define SPI_CTRL_SSE    (1UL << 7)   /* SSP 使能 */
#define SPI_CTRL_MS     (1UL << 2)   /* 主从选择 (0=主) */
#define SPI_CTRL_SPO    (1UL << 6)   /* 时钟极性 */
#define SPI_CTRL_SPH    (1UL << 5)   /* 时钟相位 */
#define SPI_CTRL_DSS_8  (0x7UL)      /* 8位数据 */

/* ---- STATUS 位定义 ---- */
#define SPI_STATUS_BSY  (1UL << 4)   /* 忙 */
#define SPI_STATUS_TNF  (1UL << 1)   /* TX FIFO 未满 */
#define SPI_STATUS_RNE  (1UL << 3)   /* RX FIFO 非空 */

/* ---- RCC ---- */
#define HPSYS_RCC_BASE  0x50000000UL
#define RCC_ENR1        (*(volatile uint32_t *)(HPSYS_RCC_BASE + 0x08))
#define RCC_MOD_SPI1    (1UL << 12)  /* SPI1 时钟使能 */

/* ---- GPIO 引脚控制 (引用 sf32lb52_gpio.c) ---- */
extern void sf32lb52_lcd_cs(int v);
extern void sf32lb52_lcd_dc(int v);
extern void sf32lb52_lcd_rst(int v);
extern void sf32lb52_backlight_on(void);
extern void sf32lb52_backlight_off(void);

/* ---- LCD 参数 ---- */
#define LCD_WIDTH   240
#define LCD_HEIGHT  320

/* ---- ST7789 命令 ---- */
#define ST7789_NOP      0x00
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVON    0x21
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A

/****************************************************************************
 * Name: sf32lb52_spi_init
 *
 * Description: 初始化 SPI1 (主机, 8位, CPOL=0, CPHA=0)
 *
 ****************************************************************************/
void sf32lb52_spi_init(void)
{
    /* 1. 使能 SPI1 时钟 */
    RCC_ENR1 |= RCC_MOD_SPI1;

    /* 2. 禁用 SPI (配置前) */
    SPI1->TOP_CTRL = 0;

    /* 3. 配置: 主机, 8位, CPOL=0, CPHA=0 */
    SPI1->TOP_CTRL = SPI_CTRL_SSE | SPI_CTRL_DSS_8;

    /* 4. 时钟分频: 系统时钟72MHz / 分频值 */
    /* 72MHz / 4 = 18MHz (LCD ST7789 最大写入约15.15MHz, 保守用9MHz) */
    SPI1->CLK_CTRL = 7;  /* 分频: 72/8 = 9MHz */

    /* 5. FIFO 配置 */
    SPI1->FIFO_CTRL = 0;

    /* 6. 使能 SPI */
    SPI1->TOP_CTRL |= SPI_CTRL_SSE;
}

/****************************************************************************
 * Name: spi_send_byte
 *
 * Description: SPI 发送/接收一个字节
 *
 ****************************************************************************/
static uint8_t spi_send_byte(uint8_t data)
{
    /* 等待 TX FIFO 有空间 */
    while (!(SPI1->STATUS & SPI_STATUS_TNF));

    /* 写入数据 */
    SPI1->DATA = (uint32_t)data;

    /* 等待发送完成 */
    while (SPI1->STATUS & SPI_STATUS_BSY);

    /* 读取接收数据 (清除 RXNE) */
    if (SPI1->STATUS & SPI_STATUS_RNE) {
        return (uint8_t)(SPI1->DATA & 0xFF);
    }
    return 0;
}

/****************************************************************************
 * Name: st7789_cmd / st7789_data
 ****************************************************************************/
static void st7789_cmd(uint8_t cmd)
{
    sf32lb52_lcd_dc(0);    /* DC = 0 (命令) */
    sf32lb52_lcd_cs(0);    /* CS = 0 (选中) */
    spi_send_byte(cmd);
    sf32lb52_lcd_cs(1);    /* CS = 1 (释放) */
}

static void st7789_data(uint8_t data)
{
    sf32lb52_lcd_dc(1);    /* DC = 1 (数据) */
    sf32lb52_lcd_cs(0);    /* CS = 0 */
    spi_send_byte(data);
    sf32lb52_lcd_cs(1);    /* CS = 1 */
}

static void st7789_data16(uint16_t data)
{
    sf32lb52_lcd_dc(1);
    sf32lb52_lcd_cs(0);
    spi_send_byte(data >> 8);
    spi_send_byte(data & 0xFF);
    sf32lb52_lcd_cs(1);
}

/****************************************************************************
 * Name: sf32lb52_lcd_init
 *
 * Description: 初始化 ST7789 LCD
 *
 ****************************************************************************/
void sf32lb52_lcd_init(void)
{
    /* 1. 初始化 SPI */
    sf32lb52_spi_init();

    /* 2. 硬件复位 */
    sf32lb52_lcd_rst(0);
    /* TODO: delay 10ms */
    sf32lb52_lcd_rst(1);
    /* TODO: delay 120ms */

    /* 3. 初始化序列 */
    st7789_cmd(ST7789_SWRESET);
    /* TODO: delay 150ms */

    st7789_cmd(ST7789_SLPOUT);
    /* TODO: delay 50ms */

    st7789_cmd(ST7789_COLMOD);
    st7789_data(0x55);    /* 16-bit RGB565 */

    st7789_cmd(ST7789_MADCTL);
    st7789_data(0x00);    /* 正常方向 */

    st7789_cmd(ST7789_INVON);   /* 颜色反转 (ST7789 需要) */

    st7789_cmd(ST7789_NORON);
    /* TODO: delay 10ms */

    st7789_cmd(ST7789_DISPON);
    /* TODO: delay 10ms */

    /* 4. 开背光 */
    sf32lb52_backlight_on();
}

/****************************************************************************
 * Name: sf32lb52_lcd_set_window
 *
 * Description: 设置绘图窗口
 *
 ****************************************************************************/
void sf32lb52_lcd_set_window(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
    st7789_cmd(ST7789_CASET);
    st7789_data(x0 >> 8);
    st7789_data(x0 & 0xFF);
    st7789_data(x1 >> 8);
    st7789_data(x1 & 0xFF);

    st7789_cmd(ST7789_RASET);
    st7789_data(y0 >> 8);
    st7789_data(y0 & 0xFF);
    st7789_data(y1 >> 8);
    st7789_data(y1 & 0xFF);

    st7789_cmd(ST7789_RAMWR);
}

/****************************************************************************
 * Name: sf32lb52_lcd_flush
 *
 * Description: 刷新指定区域像素数据
 *
 ****************************************************************************/
void sf32lb52_lcd_flush(uint16_t x0, uint16_t y0,
                         uint16_t x1, uint16_t y1,
                         const uint16_t *data, uint32_t len)
{
    sf32lb52_lcd_set_window(x0, y0, x1, y1);

    sf32lb52_lcd_dc(1);    /* DC = 1 (数据) */
    sf32lb52_lcd_cs(0);    /* CS = 0 */

    /* 发送像素数据 (每像素2字节, RGB565) */
    uint32_t i;
    for (i = 0; i < len; i++) {
        spi_send_byte(data[i] >> 8);
        spi_send_byte(data[i] & 0xFF);
    }

    sf32lb52_lcd_cs(1);    /* CS = 1 */
}

/****************************************************************************
 * Name: sf32lb52_lcd_fill
 *
 * Description: 填充指定区域为纯色
 *
 ****************************************************************************/
void sf32lb52_lcd_fill(uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t total = (uint32_t)(x1 - x0 + 1) * (y1 - y0 + 1);
    sf32lb52_lcd_set_window(x0, y0, x1, y1);

    sf32lb52_lcd_dc(1);
    sf32lb52_lcd_cs(0);

    uint32_t i;
    for (i = 0; i < total; i++) {
        spi_send_byte(color >> 8);
        spi_send_byte(color & 0xFF);
    }

    sf32lb52_lcd_cs(1);
}
