/**
 * @file sf32lb52_pm.c
 * @brief SF32LB52 低功耗管理 (STOP 模式 + 触摸唤醒)
 *
 * PMUC 基地址: 0x500ca000
 *
 * 唤醒源: PA30 (触摸 PENIRQ) 下降沿中断
 *
 * 流程:
 *   1. 关背光
 *   2. 关闭非必要外设时钟
 *   3. 配置唤醒中断
 *   4. WFI 进入 STOP
 *   5. 唤醒后恢复时钟和外设
 */
#include <nuttx/config.h>

/* ---- PMUC 寄存器定义 ---- */
typedef struct {
    volatile uint32_t CR;           /* 0x00 控制寄存器 */
    volatile uint32_t WER;          /* 0x04 唤醒使能 */
    volatile uint32_t WSR;          /* 0x08 唤醒状态 */
    volatile uint32_t WCR;          /* 0x0C 唤醒清除 */
    volatile uint32_t VRTC_CR;      /* 0x10 */
    volatile uint32_t VRET_CR;      /* 0x14 */
    volatile uint32_t LRC10_CR;     /* 0x18 */
    volatile uint32_t LRC32_CR;     /* 0x1C */
    volatile uint32_t LXT_CR;       /* 0x20 */
    volatile uint32_t AON_BG;       /* 0x24 */
    volatile uint32_t AON_LDO;      /* 0x28 */
    volatile uint32_t BUCK_CR1;     /* 0x2C */
    volatile uint32_t BUCK_CR2;     /* 0x30 */
    volatile uint32_t CHG_CR1;      /* 0x34 */
    volatile uint32_t CHG_CR2;      /* 0x38 */
    volatile uint32_t CHG_CR3;      /* 0x3C */
    volatile uint32_t CHG_CR4;      /* 0x40 */
    volatile uint32_t CHG_CR5;      /* 0x44 */
    volatile uint32_t CHG_SR;       /* 0x48 */
    volatile uint32_t HPSYS_LDO;    /* 0x4C */
    volatile uint32_t LPSYS_LDO;    /* 0x50 */
    volatile uint32_t HPSYS_SWR;    /* 0x54 */
    volatile uint32_t LPSYS_SWR;    /* 0x58 */
    volatile uint32_t PERI_LDO;     /* 0x5C */
    volatile uint32_t PMU_TR;       /* 0x60 */
    volatile uint32_t PMU_RSVD;     /* 0x64 */
    volatile uint32_t HXT_CR1;      /* 0x68 */
    volatile uint32_t HXT_CR2;      /* 0x6C */
    volatile uint32_t HXT_CR3;      /* 0x70 */
    volatile uint32_t HRC_CR;       /* 0x74 */
    volatile uint32_t DBL96_CR;     /* 0x78 */
    volatile uint32_t DBL96_CALR;   /* 0x7C */
    volatile uint32_t CAU_BGR;      /* 0x80 */
    volatile uint32_t CAU_TR;       /* 0x84 */
    volatile uint32_t CAU_RSVD;     /* 0x88 */
    volatile uint32_t WKUP_CNT;     /* 0x8C */
    volatile uint32_t PWRKEY_CNT;   /* 0x90 */
    volatile uint32_t HPSYS_VOUT;   /* 0x94 */
    volatile uint32_t LPSYS_VOUT;   /* 0x98 */
    volatile uint32_t BUCK_VOUT;    /* 0x9C */
} PMUC_TypeDef;

/* ---- 基地址 ---- */
#define PMUC_BASE       0x500ca000UL
#define PMUC            ((PMUC_TypeDef *)PMUC_BASE)

/* ---- CR 位定义 ---- */
#define PMUC_CR_SLEEP_EN    (1UL << 0)   /* 休眠使能 */

/* ---- WER 唤醒使能位 ---- */
#define PMUC_WER_PIN0       (1UL << 4)   /* 唤醒引脚0使能 */
#define PMUC_WER_PIN1       (1UL << 5)   /* 唤醒引脚1使能 */

/* ---- WSR 唤醒状态 ---- */
#define PMUC_WSR_PIN0       (1UL << 4)   /* PIN0 唤醒标志 */
#define PMUC_WSR_PIN1       (1UL << 5)   /* PIN1 唤醒标志 */

/* ---- GPIO 中断 ---- */
#define GPIO1_BASE          0x500a0000UL
#define GPIO1_IER_OFFSET    0x1C
#define GPIO1_ISR_OFFSET    0x4C

/* 触摸中断引脚 */
#define PIN_TOUCH_IRQ       30

/* ---- SCB (Cortex-M0+ 系统控制块) ---- */
#define SCB_SCR             (*(volatile uint32_t *)0xE000ED10UL)
#define SCB_SCR_SLEEPDEEP   (1UL << 2)

/* ---- 外部函数引用 ---- */
extern void sf32lb52_backlight_off(void);
extern void sf32lb52_backlight_on(void);
extern void sf32lb52_touch_irq_clear(void);
extern void sf32lb52_clockconfig(void);

/****************************************************************************
 * Name: sf32lb52_pm_init
 *
 * Description: 初始化低功耗管理 (配置唤醒源)
 *
 ****************************************************************************/
void sf32lb52_pm_init(void)
{
    /* 1. 配置唤醒引脚0 = PA30 (触摸 PENIRQ) */
    PMUC->WER |= PMUC_WER_PIN0;

    /* 2. 清除唤醒状态 */
    PMUC->WCR = 0xFFFFFFFF;

    /* 3. 确保 GPIO 中断已配置 (在 gpio_init 中已完成) */
}

/****************************************************************************
 * Name: sf32lb52_enter_stop
 *
 * Description: 进入 STOP 低功耗模式
 *
 * 唤醒后从 WFI 下一条指令继续执行
 *
 ****************************************************************************/
void sf32lb52_enter_stop(void)
{
    /* 1. 关背光 */
    sf32lb52_backlight_off();

    /* 2. 清除唤醒状态 */
    PMUC->WCR = 0xFFFFFFFF;

    /* 3. 使能休眠 */
    PMUC->CR |= PMUC_CR_SLEEP_EN;

    /* 4. 设置 SLEEPDEEP 位 */
    SCB_SCR |= SCB_SCR_SLEEPDEEP;

    /* 5. 进入 STOP (WFI) ---- MCU 在此休眠 ---- */
    __asm volatile("wfi");

    /* ---- 唤醒点 ---- */

    /* 6. 清除 SLEEPDEEP */
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;

    /* 7. 恢复系统时钟 */
    sf32lb52_clockconfig();

    /* 8. 清除触摸中断 */
    sf32lb52_touch_irq_clear();

    /* 9. 清除唤醒状态 */
    PMUC->WCR = 0xFFFFFFFF;
}

/****************************************************************************
 * Name: sf32lb52_wakeup
 *
 * Description: 唤醒后恢复
 *
 ****************************************************************************/
void sf32lb52_wakeup(void)
{
    /* 开背光 */
    sf32lb52_backlight_on();

    /* 恢复外设时钟 (由各模块 init 重新使能) */
}
