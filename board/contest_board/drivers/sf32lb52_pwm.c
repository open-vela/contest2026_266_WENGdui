/**
 * @file sf32lb52_pwm.c
 * @brief SF32LB52 PWM 驱动 (风机调速)
 *
 * 使用 GPTIM2 CH1 (PA32) 输出 PWM
 * GPTIM2 基地址: 0x500b0000
 * 频率: 1kHz, 占空比: 0-100%
 */
#include <nuttx/config.h>

/* ---- GPT 寄存器定义 ---- */
typedef struct {
    volatile uint32_t CR1;      /* 0x00 控制寄存器1 */
    volatile uint32_t CR2;      /* 0x04 */
    volatile uint32_t SMCR;     /* 0x08 */
    volatile uint32_t DIER;     /* 0x0C */
    volatile uint32_t SR;       /* 0x10 状态 */
    volatile uint32_t EGR;      /* 0x14 */
    volatile uint32_t CCMR1;    /* 0x18 捕获/比较模式1 */
    volatile uint32_t CCMR2;    /* 0x1C */
    volatile uint32_t CCER;     /* 0x20 捕获/比较使能 */
    volatile uint32_t CNT;      /* 0x24 计数器 */
    volatile uint32_t PSC;      /* 0x28 预分频 */
    volatile uint32_t ARR;      /* 0x2C 自动重载 */
    volatile uint32_t RCR;      /* 0x30 */
    volatile uint32_t CCR1;     /* 0x34 比较值1 (占空比) */
    volatile uint32_t CCR2;     /* 0x38 */
    volatile uint32_t CCR3;     /* 0x3C */
    volatile uint32_t CCR4;     /* 0x40 */
} GPT_TypeDef;

/* ---- 基地址 ---- */
#define GPTIM2_BASE     0x500b0000UL
#define GPTIM2          ((GPT_TypeDef *)GPTIM2_BASE)

/* ---- CR1 位 ---- */
#define GPT_CR1_CEN     (1UL << 0)   /* 计数器使能 */
#define GPT_CR1_ARPE    (1UL << 7)   /* 自动重载预装载 */

/* ---- CCMR1 位 (PWM 模式) ---- */
#define GPT_CCMR1_OC1M_PWM1  (0x6UL << 4)  /* PWM 模式1 */
#define GPT_CCMR1_OC1PE      (1UL << 3)    /* 预装载使能 */

/* ---- CCER 位 ---- */
#define GPT_CCER_CC1E   (1UL << 0)   /* CH1 输出使能 */

/* ---- EGR 位 ---- */
#define GPT_EGR_UG      (1UL << 0)   /* 更新事件 */

/* ---- RCC ---- */
#define HPSYS_RCC_BASE  0x50000000UL
#define RCC_ENR1        (*(volatile uint32_t *)(HPSYS_RCC_BASE + 0x08))
#define RCC_MOD_GPTIM2  (1UL << 19)  /* GPTIM2 时钟使能 */

/* 系统时钟 */
#define SYSTEM_CLOCK    72000000UL

/* PWM 参数 */
#define PWM_FREQUENCY   1000         /* 1kHz */
#define PWM_PRESCALER   (72 - 1)     /* 72MHz / 72 = 1MHz */
#define PWM_PERIOD      (1000000 / PWM_FREQUENCY - 1)  /* 1MHz / 1kHz = 1000-1 */

/****************************************************************************
 * Name: sf32lb52_pwm_init
 *
 * Description: 初始化 GPTIM2 CH1 为 PWM 输出
 *
 ****************************************************************************/
void sf32lb52_pwm_init(void)
{
    /* 1. 使能 GPTIM2 时钟 */
    RCC_ENR1 |= RCC_MOD_GPTIM2;

    /* 2. 禁用定时器 */
    GPTIM2->CR1 = 0;

    /* 3. 时基配置 */
    GPTIM2->PSC = PWM_PRESCALER;    /* 预分频: 72MHz/72 = 1MHz */
    GPTIM2->ARR = PWM_PERIOD;       /* 周期: 1MHz/1kHz = 1000 */
    GPTIM2->CCR1 = 0;               /* 初始占空比 0% */

    /* 4. PWM 模式1: OC1M=110, OC1PE=1 */
    GPTIM2->CCMR1 = GPT_CCMR1_OC1M_PWM1 | GPT_CCMR1_OC1PE;

    /* 5. 使能 CH1 输出 */
    GPTIM2->CCER = GPT_CCER_CC1E;

    /* 6. 使能自动重载预装载 */
    GPTIM2->CR1 = GPT_CR1_ARPE;

    /* 7. 生成更新事件 (加载预装载值) */
    GPTIM2->EGR = GPT_EGR_UG;

    /* 8. 使能定时器 */
    GPTIM2->CR1 |= GPT_CR1_CEN;
}

/****************************************************************************
 * Name: sf32lb52_pwm_set_duty
 *
 * Description: 设置 PWM 占空比 (0-100%)
 *
 ****************************************************************************/
void sf32lb52_pwm_set_duty(uint8_t pct)
{
    if (pct > 100) pct = 100;

    /* CCR1 = pct% * ARR */
    GPTIM2->CCR1 = (uint32_t)pct * PWM_PERIOD / 100;
}

/****************************************************************************
 * Name: sf32lb52_pwm_get_duty
 *
 * Description: 获取当前 PWM 占空比
 *
 ****************************************************************************/
uint8_t sf32lb52_pwm_get_duty(void)
{
    return (uint8_t)((uint32_t)GPTIM2->CCR1 * 100 / PWM_PERIOD);
}
