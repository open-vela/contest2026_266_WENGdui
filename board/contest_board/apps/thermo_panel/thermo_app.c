/**
 * @file thermo_app.c
 * @brief 温控应用层状态机 (NuttX POSIX API)
 */
#include "thermo_app.h"
#include "thermo_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static thermo_state_t   g_state = THERMO_STATE_BOOT;
static temp_data_t      g_temp;
static thermo_params_t  g_params;
static thermo_pid_t     g_pid;
static fault_mask_t     g_fault = FAULT_NONE;
static struct timespec  g_last_activity;

/* ---- 时间工具 ---- */
static uint32_t get_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static uint32_t idle_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t now_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    uint32_t last_ms = g_last_activity.tv_sec * 1000 + g_last_activity.tv_nsec / 1000000;
    return now_ms - last_ms;
}

/* ---- 全局访问 ---- */
thermo_state_t thermo_get_state(void) { return g_state; }
const temp_data_t *thermo_get_temp(void) { return &g_temp; }
const thermo_params_t *thermo_get_params(void) { return &g_params; }
thermo_pid_t *thermo_get_pid(void) { return &g_pid; }
fault_mask_t thermo_get_fault(void) { return g_fault; }

void thermo_mark_activity(void)
{
    clock_gettime(CLOCK_MONOTONIC, &g_last_activity);
}

void thermo_set_state(thermo_state_t s)
{
    if (s == g_state) return;
    g_state = s;
    thermo_ui_update_state(s);
}

void thermo_event(thermo_event_t ev)
{
    switch (ev) {
    case THERMO_EV_POWER:
        if (g_state == THERMO_STATE_IDLE) {
            thermo_pid_reset(&g_pid);
            thermo_set_state(THERMO_STATE_HEATING);
        } else if (g_state == THERMO_STATE_HEATING ||
                   g_state == THERMO_STATE_MAINTAIN) {
            thermo_ctrl_load_disable();
            thermo_set_state(THERMO_STATE_IDLE);
        }
        break;
    case THERMO_EV_TEMP_UP:
        g_params.target_temp += 0.5f;
        thermo_mark_activity();
        break;
    case THERMO_EV_TEMP_DOWN:
        g_params.target_temp -= 0.5f;
        thermo_mark_activity();
        break;
    case THERMO_EV_ENTER_SETTING:
        thermo_ui_show_page(1);
        thermo_mark_activity();
        break;
    case THERMO_EV_EXIT_SETTING:
        thermo_ui_show_page(0);
        thermo_mark_activity();
        break;
    case THERMO_EV_SAVE_PARAMS:
        thermo_storage_save(&g_params);
        break;
    case THERMO_EV_ALARM_ACK:
        thermo_fault_clear();
        g_fault = FAULT_NONE;
        thermo_set_state(THERMO_STATE_IDLE);
        break;
    case THERMO_EV_WAKEUP:
        backlight_on();
        thermo_mark_activity();
        thermo_ui_show_page(0);
        thermo_set_state(THERMO_STATE_IDLE);
        break;
    default: break;
    }
}

void thermo_app_poll(void)
{
    /* 1. 读温度 */
    thermo_temp_get(&g_temp);

    /* 2. 同步 UI */
    if (g_temp.valid) {
        thermo_ui_update_temp(g_temp.chamber, g_temp.ambient);
        thermo_ui_update_pid(g_pid.out_1);
    }

    /* 3. 状态机 */
    if (g_state == THERMO_STATE_HEATING || g_state == THERMO_STATE_MAINTAIN) {
        float err = g_params.target_temp - g_temp.chamber;
        float hyst = g_params.hysteresis / 10.0f;
        if (err > hyst && g_state != THERMO_STATE_HEATING)
            thermo_set_state(THERMO_STATE_HEATING);
        else if (err < -hyst && g_state != THERMO_STATE_MAINTAIN)
            thermo_set_state(THERMO_STATE_MAINTAIN);
    }

    /* 4. 故障 */
    fault_mask_t f = thermo_fault_get();
    if (f != g_fault) {
        g_fault = f;
        if (f != FAULT_NONE) {
            thermo_ui_show_alarm(f);
            thermo_ctrl_load_disable();
            thermo_set_state(THERMO_STATE_ALARM);
        }
    }

    /* 5. 闲置休眠 */
    if (g_state != THERMO_STATE_SLEEP && g_state != THERMO_STATE_ALARM &&
        g_params.idle_timeout_s > 0 &&
        idle_ms() > (uint32_t)g_params.idle_timeout_s * 1000) {
        thermo_set_state(THERMO_STATE_SLEEP);
        backlight_off();
    }
}

void thermo_app_init(void)
{
    memset(&g_temp, 0, sizeof(g_temp));
    thermo_pid_reset(&g_pid);
    g_pid.kp = 6.0f; g_pid.ki = 0.2f; g_pid.kd = 1.0f;
    g_pid.out_min = 0; g_pid.out_max = 100;

    thermo_storage_load(&g_params);
    thermo_temp_init();
    thermo_ctrl_init();
    thermo_fault_init();

    clock_gettime(CLOCK_MONOTONIC, &g_last_activity);
    thermo_set_state(THERMO_STATE_IDLE);
    printf("[THERMO] app init, target=%.1f\n", g_params.target_temp);
}
