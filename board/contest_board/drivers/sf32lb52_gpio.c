/**
 * @file sf32lb52_gpio.c
 * @brief SF32LB52 GPIO 驱动 (NuttX)
 *
 * GPIO1 基地址: 0x500a0000 (HPSYS, 最多96引脚)
 * GPIO2 基地址: 0x40080000 (LPSYS, 最多64引脚)
 *
 * 引脚分配:
 *   PA18 - LCD CS      (输出)
 *   PA19 - LCD DC      (输出)
 *   PA20 - LCD RST     (输出)
 *   PA25 - 背光控制     (输出)
 *   PA26 - 故障指示灯   (输出)
 *   PA30 - 触摸 PENIRQ (输入/中断)
 *   PA33 - 加热继电器   (输出)
 *   PA34 - 电源按键     (输入)
 *   PA11 - 设置按键     (输入)
 */
#include <nuttx/config.h>
#include <nuttx/ioexpander/gpio.h>

/* ---- GPIO 寄存器定义 ---- */
typedef struct {
    volatile uint32_t DIR;      /* 0x00 方向寄存器 (1=输出, 0=输入) */
    volatile uint32_t DOR;      /* 0x04 数据输出寄存器 */
    volatile uint32_t DOSR;     /* 0x08 数据输出置位 (写1置位) */
    volatile uint32_t DOCR;     /* 0x0C 数据输出清除 (写1清除) */
    volatile uint32_t DOER;     /* 0x10 数据输出使能 */
    volatile uint32_t DOESR;    /* 0x14 输出使能置位 */
    volatile uint32_t DOECR;    /* 0x18 输出使能清除 */
    volatile uint32_t IER;      /* 0x1C 中断使能 */
    volatile uint32_t IESR;     /* 0x20 中断使能置位 */
    volatile uint32_t IECR;     /* 0x24 中断使能清除 */
    volatile uint32_t ITR;      /* 0x28 中断类型 (边沿/电平) */
    volatile uint32_t ITSR;     /* 0x2C 中断类型置位 */
    volatile uint32_t ITCR;     /* 0x30 中断类型清除 */
    volatile uint32_t IPHR;     /* 0x34 中断高电平有效 */
    volatile uint32_t IPHSR;    /* 0x38 */
    volatile uint32_t IPHCR;    /* 0x3C */
    volatile uint32_t IPLR;     /* 0x40 中断低电平有效 */
    volatile uint32_t IPLSR;    /* 0x44 */
    volatile uint32_t IPLCR;    /* 0x48 */
    volatile uint32_t ISR;      /* 0x4C 中断状态 */
} GPIO_TypeDef;

/* ---- 基地址 ---- */
#define GPIO1_BASE      0x500a0000UL
#define GPIO1           ((GPIO_TypeDef *)GPIO1_BASE)

/* ---- RCC 使能 ---- */
#define HPSYS_RCC_BASE  0x50000000UL
#define RCC_ENR1        (*(volatile uint32_t *)(HPSYS_RCC_BASE + 0x08))
#define RCC_MOD_GPIO1   (1UL << 24)  /* GPIO1 时钟使能位 */

/* ---- 引脚定义 ---- */
#define PIN_LCD_CS      18
#define PIN_LCD_DC      19
#define PIN_LCD_RST     20
#define PIN_BACKLIGHT   25
#define PIN_FAULT_LED   26
#define PIN_TOUCH_IRQ   30
#define PIN_HEATER      33
#define PIN_KEY_POWER   34
#define PIN_KEY_SETTING 11

/* ---- PINMUX ---- */
#define PINMUX1_BASE    0x50003000UL

/****************************************************************************
 * Name: sf32lb52_gpio_init
 *
 * Description: 初始化所有 GPIO 引脚
 *
 ****************************************************************************/
void sf32lb52_gpio_init(void)
{
    /* 1. 使能 GPIO1 时钟 */
    RCC_ENR1 |= RCC_MOD_GPIO1;

    /* 2. 配置输出引脚方向 (DIR 寄存器, 1=输出) */
    GPIO1->DIR |= (1UL << PIN_LCD_CS)
               |  (1UL << PIN_LCD_DC)
               |  (1UL << PIN_LCD_RST)
               |  (1UL << PIN_BACKLIGHT)
               |  (1UL << PIN_FAULT_LED)
               |  (1UL << PIN_HEATER);

    /* 3. 配置输入引脚 (DIR=0 即输入, 默认) */
    GPIO1->DIR &= ~((1UL << PIN_TOUCH_IRQ)
                   | (1UL << PIN_KEY_POWER)
                   | (1UL << PIN_KEY_SETTING));

    /* 4. 使能输出 (DOER) */
    GPIO1->DOESR = (1UL << PIN_LCD_CS)
                 | (1UL << PIN_LCD_DC)
                 | (1UL << PIN_LCD_RST)
                 | (1UL << PIN_BACKLIGHT)
                 | (1UL << PIN_FAULT_LED)
                 | (1UL << PIN_HEATER);

    /* 5. 默认输出状态 */
    GPIO1->DOSR = (1UL << PIN_LCD_CS);    /* CS 高 (不选中) */
    GPIO1->DOCR = (1UL << PIN_LCD_DC);    /* DC 低 (命令模式) */
    GPIO1->DOCR = (1UL << PIN_LCD_RST);   /* RST 低 */
    GPIO1->DOCR = (1UL << PIN_BACKLIGHT); /* 背光关 */
    GPIO1->DOCR = (1UL << PIN_FAULT_LED); /* LED 灭 */
    GPIO1->DOCR = (1UL << PIN_HEATER);    /* 继电器断开 */

    /* 6. 配置触摸中断 (PA30, 下降沿) */
    GPIO1->ITCR = (1UL << PIN_TOUCH_IRQ);  /* 清除边沿/电平选择 */
    GPIO1->ITSR = (1UL << PIN_TOUCH_IRQ);  /* 边沿触发 */
    GPIO1->IPLSR = (1UL << PIN_TOUCH_IRQ); /* 下降沿有效 */
    GPIO1->IESR = (1UL << PIN_TOUCH_IRQ);  /* 使能中断 */
}

/****************************************************************************
 * Name: sf32lb52_gpio_write
 *
 * Description: 设置指定引脚输出电平
 *
 ****************************************************************************/
void sf32lb52_gpio_write(int pin, int value)
{
    if (value) {
        GPIO1->DOSR = (1UL << pin);  /* 置位 (输出高) */
    } else {
        GPIO1->DOCR = (1UL << pin);  /* 清除 (输出低) */
    }
}

/****************************************************************************
 * Name: sf32lb52_gpio_read
 *
 * Description: 读取指定引脚输入电平
 *
 ****************************************************************************/
int sf32lb52_gpio_read(int pin)
{
    return (GPIO1->DOR >> pin) & 1;
}

/****************************************************************************
 * Name: 背光控制
 ****************************************************************************/
void sf32lb52_backlight_on(void)
{
    sf32lb52_gpio_write(PIN_BACKLIGHT, 1);
}

void sf32lb52_backlight_off(void)
{
    sf32lb52_gpio_write(PIN_BACKLIGHT, 0);
}

/****************************************************************************
 * Name: 加热继电器控制
 ****************************************************************************/
void sf32lb52_heater_on(void)
{
    sf32lb52_gpio_write(PIN_HEATER, 1);
}

void sf32lb52_heater_off(void)
{
    sf32lb52_gpio_write(PIN_HEATER, 0);
}

/****************************************************************************
 * Name: 故障指示灯控制
 ****************************************************************************/
void sf32lb52_fault_led_on(void)
{
    sf32lb52_gpio_write(PIN_FAULT_LED, 1);
}

void sf32lb52_fault_led_off(void)
{
    sf32lb52_gpio_write(PIN_FAULT_LED, 0);
}

/****************************************************************************
 * Name: LCD 控制引脚
 ****************************************************************************/
void sf32lb52_lcd_cs(int v)   { sf32lb52_gpio_write(PIN_LCD_CS, v); }
void sf32lb52_lcd_dc(int v)   { sf32lb52_gpio_write(PIN_LCD_DC, v); }
void sf32lb52_lcd_rst(int v)  { sf32lb52_gpio_write(PIN_LCD_RST, v); }

/****************************************************************************
 * Name: 按键读取
 ****************************************************************************/
int sf32lb52_key_power_read(void)
{
    return sf32lb52_gpio_read(PIN_KEY_POWER);
}

int sf32lb52_key_setting_read(void)
{
    return sf32lb52_gpio_read(PIN_KEY_SETTING);
}

/****************************************************************************
 * Name: 触摸中断状态
 ****************************************************************************/
int sf32lb52_touch_irq_pending(void)
{
    return (GPIO1->ISR >> PIN_TOUCH_IRQ) & 1;
}

void sf32lb52_touch_irq_clear(void)
{
    /* 写1清除中断标志 */
    volatile uint32_t *isr_reg = &GPIO1->ISR;
    *isr_reg = (1UL << PIN_TOUCH_IRQ);
}
