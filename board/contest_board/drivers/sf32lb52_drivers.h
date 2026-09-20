/**
 * @file sf32lb52_drivers.h
 * @brief SF32LB52 硬件驱动总头文件
 */
#ifndef SF32LB52_DRIVERS_H
#define SF32LB52_DRIVERS_H

#include <stdint.h>

/* ---- UART (sf32lb52_uart.c) ---- */
void sf32lb52_uart_init(void);
int  sf32lb52_uart_putc(int ch);
int  sf32lb52_uart_getc(void);
void sf32lb52_uart_puts(const char *str);

/* ---- GPIO (sf32lb52_gpio.c) ---- */
void sf32lb52_gpio_init(void);
void sf32lb52_gpio_write(int pin, int value);
int  sf32lb52_gpio_read(int pin);
void sf32lb52_backlight_on(void);
void sf32lb52_backlight_off(void);
void sf32lb52_heater_on(void);
void sf32lb52_heater_off(void);
void sf32lb52_fault_led_on(void);
void sf32lb52_fault_led_off(void);
void sf32lb52_lcd_cs(int v);
void sf32lb52_lcd_dc(int v);
void sf32lb52_lcd_rst(int v);
int  sf32lb52_key_power_read(void);
int  sf32lb52_key_setting_read(void);
int  sf32lb52_touch_irq_pending(void);
void sf32lb52_touch_irq_clear(void);

/* ---- ADC (sf32lb52_adc.c) ---- */
void     sf32lb52_adc_init(void);
uint16_t sf32lb52_adc_read_channel(uint8_t channel);
int      sf32lb52_adc_read_temp(uint8_t channel, int *temp_x10);

/* ---- SPI + LCD (sf32lb52_spi_lcd.c) ---- */
void sf32lb52_spi_init(void);
void sf32lb52_lcd_init(void);
void sf32lb52_lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void sf32lb52_lcd_flush(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                         const uint16_t *data, uint32_t len);
void sf32lb52_lcd_fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                        uint16_t color);

/* ---- PWM (sf32lb52_pwm.c) ---- */
void    sf32lb52_pwm_init(void);
void    sf32lb52_pwm_set_duty(uint8_t pct);
uint8_t sf32lb52_pwm_get_duty(void);

/* ---- 低功耗 (sf32lb52_pm.c) ---- */
void sf32lb52_pm_init(void);
void sf32lb52_enter_stop(void);
void sf32lb52_wakeup(void);

/* ---- 引脚定义 ---- */
enum {
    PIN_LCD_CS    = 18,
    PIN_LCD_DC    = 19,
    PIN_LCD_RST   = 20,
    PIN_BACKLIGHT = 25,
    PIN_FAULT_LED = 26,
    PIN_TOUCH_IRQ = 30,
    PIN_FAN_PWM   = 32,
    PIN_HEATER    = 33,
    PIN_KEY_POWER = 34,
    PIN_KEY_SET   = 11,
};

#endif
