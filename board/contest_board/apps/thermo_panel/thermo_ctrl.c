/**
 * @file thermo_ctrl.c
 * @brief PID + PWM 风机 + 继电器 (NuttX PWM/GPIO 接口)
 */
#include "thermo_app.h"
#include <nuttx/config.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/timers/pwm.h>
#include <string.h>

#define HEATER_ON_DELTA  2.0f
#define HEATER_OFF_DELTA 0.0f
#define FAN_MIN_HEAT     30

static int g_pwm_fd = -1;
static int g_relay_fd = -1;
static bool g_heater_on = false;

float thermo_pid_step(thermo_pid_t *p, float target, float measured) {
    if (!p) return 0;
    float ek = target - measured;
    float du = p->kp*(ek - p->ek_1) + p->ki*ek + p->kd*(ek - 2*p->ek_1 + p->ek_2);
    float u = p->out_1 + du;
    if (u < p->out_min) u = p->out_min;
    if (u > p->out_max) u = p->out_max;
    p->ek_2 = p->ek_1; p->ek_1 = ek; p->out_1 = u;
    return u;
}

void thermo_pid_reset(thermo_pid_t *p) {
    if (p) { p->ek_1 = p->ek_2 = p->out_1 = 0; }
}

void thermo_ctrl_fan_set(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (g_pwm_fd >= 0) {
        struct pwm_info_s info;
        memset(&info, 0, sizeof(info));
        info.frequency = 1000;
        /* NuttX: duty 为 ub16_t (uint16_t), 满量程 0xFFFF 对应 100% */
        info.duty = (ub16_t)((uint32_t)0xFFFF * pct / 100);
        ioctl(g_pwm_fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info);
        ioctl(g_pwm_fd, PWMIOC_START, 0);
    }
}

void thermo_ctrl_heater_on(void) {
    if (g_relay_fd >= 0) { int v = 1; ioctl(g_relay_fd, GPIOC_WRITE, (unsigned long)&v); }
    g_heater_on = true;
}

void thermo_ctrl_heater_off(void) {
    if (g_relay_fd >= 0) { int v = 0; ioctl(g_relay_fd, GPIOC_WRITE, (unsigned long)&v); }
    g_heater_on = false;
}

void thermo_ctrl_load_disable(void) {
    thermo_ctrl_fan_set(0);
    thermo_ctrl_heater_off();
}

void thermo_ctrl_init(void) {
    g_heater_on = false;
    g_pwm_fd = open("/dev/pwm2", O_RDONLY);
    g_relay_fd = open("/dev/gpio1", O_WRONLY);
    thermo_ctrl_fan_set(0);
    thermo_ctrl_heater_off();
    printf("[THERMO] ctrl init (pwm=%d, relay=%d)\n", g_pwm_fd, g_relay_fd);
}
