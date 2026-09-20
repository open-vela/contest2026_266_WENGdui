/**
 * @file thermo_temp.c
 * @brief NTC 温度采集 (NuttX ADC 设备接口)
 */
#include "thermo_app.h"
#include <nuttx/config.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <string.h>

#define NTC_VREF       3.3f
#define NTC_ADC_MAX    4095.0f
#define NTC_FIXED_R    10000.0f
#define FILTER_N       8

static const float s_ntc_rt[] = {
    67710,42330,27280,18070,10000,6370,4160,2800,1940,1382,1006,749,569,440
};
#define TABLE_LEN (sizeof(s_ntc_rt)/sizeof(s_ntc_rt[0]))
#define TABLE_STEP 10.0f
#define TABLE_MIN (-10.0f)

typedef struct { uint16_t buf[FILTER_N]; uint8_t idx,cnt; int32_t sum; } filter_t;
static filter_t g_filt[2];
static temp_data_t g_last;
static int g_adc_fd = -1;

static float ntc_r2t(float r) {
    if (r <= s_ntc_rt[TABLE_LEN-1]) return 110;
    if (r >= s_ntc_rt[0]) return -10;
    for (unsigned i = 0; i < TABLE_LEN-1; i++) {
        if (r <= s_ntc_rt[i] && r >= s_ntc_rt[i+1]) {
            float t1 = TABLE_MIN + i*TABLE_STEP, t2 = t1+TABLE_STEP;
            return t1 + (t2-t1)*(s_ntc_rt[i]-r)/(s_ntc_rt[i]-s_ntc_rt[i+1]);
        }
    }
    return -10;
}

static uint16_t filt_push(filter_t *f, uint16_t v) {
    if (f->cnt == FILTER_N) f->sum -= f->buf[f->idx]; else f->cnt++;
    f->buf[f->idx] = v; f->sum += v;
    f->idx = (f->idx+1) % FILTER_N;
    return (uint16_t)(f->sum / f->cnt);
}

/* 读取一条 ADC 采样消息, 返回其所属通道与数值。
 * NuttX ADC 驱动以 adc_msg_s 流形式返回组内所有通道,
 * 须依据 am_channel 字段分发, 不能假设 read 顺序。 */
static int adc_read_msg(int *ch, uint16_t *val) {
    struct adc_msg_s msg;
    if (g_adc_fd < 0) return -1;
    int ret = read(g_adc_fd, &msg, sizeof(msg));
    if (ret != sizeof(msg)) return -1;
    *ch  = msg.am_channel;
    *val = (uint16_t)(msg.am_data & 0xFFF);
    return 0;
}

void thermo_temp_init(void) {
    memset(g_filt, 0, sizeof(g_filt));
    memset(&g_last, 0, sizeof(g_last));
    g_adc_fd = open("/dev/adc0", O_RDONLY);
    if (g_adc_fd < 0) printf("[THERMO] WARN: ADC open failed\n");
    else printf("[THERMO] ADC init OK\n");
}

void thermo_temp_get(temp_data_t *out) {
    uint16_t raw[2] = {0, 0};
    bool got[2] = {false, false};

    /* 读取若干条 ADC 消息, 按 am_channel 分发到 0/1 通道 */
    for (int i = 0; i < 16 && !(got[0] && got[1]); i++) {
        int ch;
        uint16_t val;
        if (adc_read_msg(&ch, &val) < 0) break;
        if (ch == 0 || ch == 1) {
            raw[ch] = val;
            got[ch] = true;
        }
    }

    /* 周期采样 */
    for (int ch = 0; ch < 2; ch++) {
        if (!got[ch]) {
            if (ch == 0) g_last.chamber = -999;
            else g_last.ambient = -999;
            continue;
        }
        if (raw[ch] > (uint16_t)(NTC_ADC_MAX * 0.98f)) {
            g_filt[ch].cnt = g_filt[ch].sum = g_filt[ch].idx = 0;
            if (ch == 0) g_last.chamber = -999;
            else g_last.ambient = -999;
            continue;
        }
        uint16_t avg = filt_push(&g_filt[ch], raw[ch]);
        float v = (avg / NTC_ADC_MAX) * NTC_VREF;
        float r = (v > 0.001f) ? NTC_FIXED_R * (NTC_VREF/v - 1) : 1e9f;
        float temp = ntc_r2t(r);
        if (ch == 0) g_last.chamber = temp;
        else g_last.ambient = temp;
    }
    g_last.valid = (g_last.chamber > -100 && g_last.ambient > -100);
    if (out) memcpy(out, &g_last, sizeof(g_last));
}
