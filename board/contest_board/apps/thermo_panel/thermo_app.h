/**
 * @file thermo_app.h
 * @brief 温控应用层接口 (OpenVela/NuttX)
 */
#ifndef THERMO_APP_H
#define THERMO_APP_H

#include <stdint.h>
#include <stdbool.h>

/* 状态 */
typedef enum {
    THERMO_STATE_BOOT = 0, THERMO_STATE_IDLE, THERMO_STATE_HEATING,
    THERMO_STATE_MAINTAIN, THERMO_STATE_ALARM, THERMO_STATE_SLEEP,
} thermo_state_t;

/* 故障掩码 */
typedef enum {
    FAULT_NONE = 0, FAULT_OVER_TEMP = 1, FAULT_SENSOR_BREAK = 2,
    FAULT_ADC_ERROR = 4, FAULT_UNDER_TEMP = 8,
} fault_mask_t;

/* 事件 */
typedef enum {
    THERMO_EV_NONE = 0, THERMO_EV_POWER, THERMO_EV_TEMP_UP, THERMO_EV_TEMP_DOWN,
    THERMO_EV_ENTER_SETTING, THERMO_EV_EXIT_SETTING, THERMO_EV_SAVE_PARAMS,
    THERMO_EV_ALARM_ACK, THERMO_EV_WAKEUP,
} thermo_event_t;

/* 温度数据 */
typedef struct {
    float chamber, ambient;
    bool valid;
    uint32_t ts_ms;
} temp_data_t;

/* 参数 */
typedef struct __attribute__((packed)) {
    uint32_t version;
    float target_temp, temp_high_limit, temp_low_limit;
    uint16_t idle_timeout_s, timer_on_min, timer_off_min;
    uint16_t fan_min_duty, hysteresis;
    uint32_t run_hours, crc32;
} thermo_params_t;

/* PID */
typedef struct {
    float kp, ki, kd, out_min, out_max;
    float ek_1, ek_2, out_1;
} thermo_pid_t;

/* 全局参数对象 —— 厂家 app 层同名全局 (厂家 thermo_ui.c 直接写
 *   g_thermo_params.target_temp / .temp_high_limit)。
 *   为保证"厂家 UI 原文"能编译且数据唯一, 本工程也用它作为唯一实例。 */
extern thermo_params_t g_thermo_params;

/* API */
thermo_state_t thermo_get_state(void);
const temp_data_t *thermo_get_temp(void);
const thermo_params_t *thermo_get_params(void);
thermo_pid_t *thermo_get_pid(void);
fault_mask_t thermo_get_fault(void);
void thermo_set_state(thermo_state_t s);
void thermo_event(thermo_event_t ev);
void thermo_mark_activity(void);
void thermo_app_init(void);
void thermo_app_poll(void);

/* 子模块 */
void thermo_temp_init(void);
void thermo_temp_get(temp_data_t *out);
void thermo_ctrl_init(void);
void thermo_ctrl_load_disable(void);
void thermo_ctrl_fan_set(uint8_t pct);
void thermo_ctrl_heater_on(void);
void thermo_ctrl_heater_off(void);
float thermo_pid_step(thermo_pid_t *p, float target, float measured);
void thermo_pid_reset(thermo_pid_t *p);
void thermo_fault_init(void);
fault_mask_t thermo_fault_get(void);
void thermo_fault_clear(void);
void thermo_storage_load(thermo_params_t *p);
void thermo_storage_save(const thermo_params_t *p);

/* 背光 */
void backlight_on(void);
void backlight_off(void);

#endif
