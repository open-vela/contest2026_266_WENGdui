/**
 * @file sf32lb52_adc.c
 * @brief SF32LB52 GPADC 驱动 (NTC 温度采集)
 *
 * GPADC 基地址: 0x50087000
 * 通道: CH0=PA28(腔体NTC), CH1=PA29(环境NTC)
 * 分辨率: 10-bit (0-1023)
 *
 * NTC电路: VCC(3.3V) -- NTC -- ADC -- R_fixed(10k) -- GND
 *   V_adc = 3.3 * R_fixed / (R_ntc + R_fixed)
 *   R_ntc = R_fixed * (3.3 / V_adc - 1)
 */
#include <nuttx/config.h>
#include <string.h>

/* ---- GPADC 寄存器定义 ---- */
typedef struct {
    volatile uint32_t CFG_REG1;     /* 0x00 配置寄存器1 */
    volatile uint32_t SLOT0_REG;    /* 0x04 通道槽0配置 */
    volatile uint32_t SLOT1_REG;    /* 0x08 */
    volatile uint32_t SLOT2_REG;    /* 0x0C */
    volatile uint32_t SLOT3_REG;    /* 0x10 */
    volatile uint32_t SLOT4_REG;    /* 0x14 */
    volatile uint32_t SLOT5_REG;    /* 0x18 */
    volatile uint32_t SLOT6_REG;    /* 0x1C */
    volatile uint32_t SLOT7_REG;    /* 0x20 */
    volatile uint32_t RDATA0;       /* 0x24 读数据0 */
    volatile uint32_t RDATA1;       /* 0x28 读数据1 */
    volatile uint32_t RDATA2;       /* 0x2C */
    volatile uint32_t RDATA3;       /* 0x30 */
    volatile uint32_t DMA_RDATA;    /* 0x34 DMA数据 */
    volatile uint32_t CTRL_REG;     /* 0x38 控制寄存器 */
    volatile uint32_t CTRL_REG2;    /* 0x3C 控制寄存器2 */
    volatile uint32_t STATUS;       /* 0x40 状态 */
    volatile uint32_t IRQ;          /* 0x44 中断 */
} GPADC_TypeDef;

/* ---- 基地址 ---- */
#define GPADC_BASE      0x50087000UL
#define GPADC           ((GPADC_TypeDef *)GPADC_BASE)

/* ---- RCC ---- */
#define HPSYS_RCC_BASE  0x50000000UL
#define RCC_ENR2        (*(volatile uint32_t *)(HPSYS_RCC_BASE + 0x0C))
#define RCC_MOD_GPADC   (1UL << 8)   /* GPADC 时钟使能位 */

/* ---- 配置位定义 ---- */
#define GPADC_CFG_SE        (1UL << 0)   /* 单端模式 */
#define GPADC_CFG_LDOREF_EN (1UL << 1)   /* LDO参考使能 */

/* SLOT 寄存器位 */
#define GPADC_SLOT_EN       (1UL << 0)   /* 通道使能 */
#define GPADC_SLOT_PCHNL(x) ((x) << 4)  /* 正通道选择 */
#define GPADC_SLOT_ACC(x)   ((x) << 8)  /* 累加次数 */

/* CTRL 寄存器位 */
#define GPADC_CTRL_START    (1UL << 0)   /* 启动转换 */
#define GPADC_CTRL_STOP     (1UL << 1)   /* 停止转换 */
#define GPADC_CTRL_OP_CONT  (1UL << 4)   /* 连续模式 */

/* STATUS 寄存器位 */
#define GPADC_STATUS_DONE   (1UL << 0)   /* 转换完成 */

/* ---- NTC 参数 ---- */
#define NTC_VREF        3.3f
#define NTC_ADC_MAX     1023.0f   /* 10-bit */
#define NTC_FIXED_R     10000.0f

/* R-T 查表 (-10~110°C, step 10°C) */
static const float s_ntc_rt[] = {
    67710, 42330, 27280, 18070, 10000,
    6370,  4160,  2800,  1940,  1382,
    1006,  749,   569,   440
};
#define NTC_TABLE_LEN  14
#define NTC_TABLE_STEP 10.0f
#define NTC_TABLE_MIN  (-10.0f)

/* 滤波 */
#define FILTER_N  8
typedef struct {
    uint16_t buf[FILTER_N];
    uint8_t  idx;
    uint8_t  cnt;
    uint32_t sum;
} filter_t;

static filter_t s_filt[2];   /* CH0=腔体, CH1=环境 */

/****************************************************************************
 * Name: ntc_r_to_temp
 *
 * Description: NTC 电阻 → 温度 (查表+线性插值)
 *
 ****************************************************************************/
static float ntc_r_to_temp(float r)
{
    int i;
    if (r <= s_ntc_rt[NTC_TABLE_LEN - 1]) return 110.0f;
    if (r >= s_ntc_rt[0])                  return -10.0f;

    for (i = 0; i < NTC_TABLE_LEN - 1; i++) {
        if (r <= s_ntc_rt[i] && r >= s_ntc_rt[i + 1]) {
            float t1 = NTC_TABLE_MIN + i * NTC_TABLE_STEP;
            float t2 = t1 + NTC_TABLE_STEP;
            return t1 + (t2 - t1) * (s_ntc_rt[i] - r) /
                   (s_ntc_rt[i] - s_ntc_rt[i + 1]);
        }
    }
    return -10.0f;
}

/****************************************************************************
 * Name: filter_push
 *
 * Description: 滑动均值滤波
 *
 ****************************************************************************/
static uint16_t filter_push(filter_t *f, uint16_t v)
{
    if (f->cnt == FILTER_N) {
        f->sum -= f->buf[f->idx];
    } else {
        f->cnt++;
    }
    f->buf[f->idx] = v;
    f->sum += v;
    f->idx = (f->idx + 1) % FILTER_N;
    return (uint16_t)(f->sum / f->cnt);
}

/****************************************************************************
 * Name: sf32lb52_adc_init
 *
 * Description: 初始化 GPADC
 *
 ****************************************************************************/
void sf32lb52_adc_init(void)
{
    /* 1. 使能 GPADC 时钟 */
    RCC_ENR2 |= RCC_MOD_GPADC;

    /* 2. 配置: 单端模式, LDO参考使能 */
    GPADC->CFG_REG1 = GPADC_CFG_SE | GPADC_CFG_LDOREF_EN;

    /* 3. 配置通道槽0 (CH0 = 腔体NTC) */
    GPADC->SLOT0_REG = GPADC_SLOT_EN | GPADC_SLOT_PCHNL(0);

    /* 4. 配置通道槽1 (CH1 = 环境NTC) */
    GPADC->SLOT1_REG = GPADC_SLOT_EN | GPADC_SLOT_PCHNL(1);

    /* 5. 配置: 采样宽度, 转换宽度 */
    GPADC->CTRL_REG2 = (75 << 0) | (71 << 8);  /* 默认值 */

    /* 6. 清零滤波缓冲 */
    memset(s_filt, 0, sizeof(s_filt));
}

/****************************************************************************
 * Name: sf32lb52_adc_read_channel
 *
 * Description: 读取指定通道 ADC 原始值 (0-1023)
 *
 ****************************************************************************/
uint16_t sf32lb52_adc_read_channel(uint8_t channel)
{
    volatile uint32_t *slot_reg;
    volatile uint32_t *rdata_reg;

    /* 选择通道槽和数据寄存器 */
    switch (channel) {
    case 0:
        slot_reg = &GPADC->SLOT0_REG;
        rdata_reg = &GPADC->RDATA0;
        break;
    case 1:
        slot_reg = &GPADC->SLOT1_REG;
        rdata_reg = &GPADC->RDATA1;
        break;
    default:
        return 0;
    }

    /* 配置通道 */
    *slot_reg = GPADC_SLOT_EN | GPADC_SLOT_PCHNL(channel);

    /* 启动单次转换 */
    GPADC->CTRL_REG = GPADC_CTRL_START;

    /* 等待完成 (超时保护) */
    int timeout = 10000;
    while (!(GPADC->STATUS & GPADC_STATUS_DONE) && --timeout > 0);

    if (timeout == 0) return 0;

    /* 读取结果 (10-bit) */
    uint16_t raw = (uint16_t)(*rdata_reg & 0x3FF);

    /* 清除完成标志 */
    GPADC->STATUS = GPADC_STATUS_DONE;

    return raw;
}

/****************************************************************************
 * Name: sf32lb52_adc_read_temp
 *
 * Description: 读取指定通道温度 (°C * 10, 整数)
 *
 ****************************************************************************/
int sf32lb52_adc_read_temp(uint8_t channel, int *temp_x10)
{
    uint16_t raw = sf32lb52_adc_read_channel(channel);

    /* 断线检测: 接近满量程 */
    if (raw > (uint16_t)(NTC_ADC_MAX * 0.98f)) {
        return -1;  /* 传感器断线 */
    }

    /* 滑动均值滤波 */
    uint16_t avg = filter_push(&s_filt[channel], raw);

    /* ADC → 电压 */
    float v = ((float)avg / NTC_ADC_MAX) * NTC_VREF;

    /* 电压 → NTC电阻 */
    float r = (v > 0.001f) ? NTC_FIXED_R * (NTC_VREF / v - 1.0f) : 1e9f;

    /* 电阻 → 温度 */
    float temp = ntc_r_to_temp(r);

    *temp_x10 = (int)(temp * 10.0f + 0.5f);
    return 0;  /* 成功 */
}
