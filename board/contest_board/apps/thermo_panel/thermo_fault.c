/**
 * @file thermo_fault.c
 * @brief 故障检测 (超温/断线)
 */
#include "thermo_app.h"
#include <string.h>

static fault_mask_t g_fault = FAULT_NONE;
static bool g_overtemp_latched = false;

void thermo_fault_init(void) {
    g_fault = FAULT_NONE;
    g_overtemp_latched = false;
}

fault_mask_t thermo_fault_get(void) {
    const temp_data_t *t = thermo_get_temp();
    const thermo_params_t *p = thermo_get_params();
    fault_mask_t f = FAULT_NONE;

    if (!t->valid) {
        f |= FAULT_SENSOR_BREAK;
        if (t->chamber < -100 || t->ambient < -100) f |= FAULT_ADC_ERROR;
    }

    if (t->valid && t->chamber > p->temp_high_limit) g_overtemp_latched = true;
    else if (t->valid && t->chamber < p->temp_low_limit) g_overtemp_latched = false;
    if (g_overtemp_latched) f |= FAULT_OVER_TEMP;

    g_fault = f;
    return f;
}

void thermo_fault_clear(void) {
    g_fault = FAULT_NONE;
    g_overtemp_latched = false;
}
