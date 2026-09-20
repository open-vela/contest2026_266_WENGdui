/**
 * @file thermo_main.c
 * @brief SF32LB52 温控面板 OpenVela (NuttX) 应用入口
 *
 * 基于 POSIX API + LVGL + NuttX 设备驱动框架
 *
 * LVGL 在 NuttX 中不是自动运行的, 需要:
 *   - LVGL v9: lv_init() + lv_tick_set_cb() + lv_timer_handler()
 *   - LVGL v8: lv_init() + 周期性 lv_tick_inc() + lv_timer_handler()
 * 本文件用 LVGL_VERSION_MAJOR 兼容两种版本。
 */
#include <nuttx/config.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>

#include <lvgl.h>

#include <nuttx/ioexpander/gpio.h>

#include "thermo_app.h"
#include "thermo_ui.h"

/* ---- 单调毫秒时钟 ---- */

static uint32_t thermo_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

#if LVGL_VERSION_MAJOR >= 9
static uint32_t lv_tick_get_cb(void)
{
    return thermo_millis();
}
#endif

/* ---- 背光控制 ---- */

static int g_bl_fd = -1;

static void backlight_init(void)
{
    g_bl_fd = open("/dev/gpio0", O_WRONLY);
    if (g_bl_fd >= 0) {
        int val = 1;
        ioctl(g_bl_fd, GPIOC_WRITE, (unsigned long)&val);
    } else {
        printf("[THERMO] WARN: backlight gpio open failed\n");
    }
}

void backlight_on(void)
{
    if (g_bl_fd >= 0) {
        int val = 1;
        ioctl(g_bl_fd, GPIOC_WRITE, (unsigned long)&val);
    }
}

void backlight_off(void)
{
    if (g_bl_fd >= 0) {
        int val = 0;
        ioctl(g_bl_fd, GPIOC_WRITE, (unsigned long)&val);
    }
}

/**
 * @brief 温控面板主入口 (NuttX user application)
 */
int main(int argc, char *argv[])
{
    printf("===========================================\n");
    printf("  SF32LB52 Thermo Panel (OpenVela/NuttX)\n");
    printf("  LVGL + NTC + PID + Flash Storage\n");
    printf("===========================================\n");

    /* 1. 背光初始化 */
    backlight_init();

    /* 2. LVGL 初始化 */
    lv_init();
#if LVGL_VERSION_MAJOR >= 9
    lv_tick_set_cb(lv_tick_get_cb);
    printf("[THERMO] LVGL v%d (tick callback)\n", LVGL_VERSION_MAJOR);
#else
    printf("[THERMO] LVGL v%d (tick inc)\n", LVGL_VERSION_MAJOR);
#endif
    thermo_ui_init();    /* 创建所有屏幕/控件 */

    /* 3. 温控应用初始化 (temp/ctrl/fault/storage) */
    thermo_app_init();

    printf("[THERMO] Init done, entering main loop\n");

    /* 4. 主循环: LVGL + 应用轮询 */
    uint32_t last_lv_handler = thermo_millis();
#if LVGL_VERSION_MAJOR < 9
    uint32_t last_tick = last_lv_handler;
#endif

    while (1) {
        uint32_t now = thermo_millis();

#if LVGL_VERSION_MAJOR < 9
        /* v8: 手动推进 LVGL 心跳 */
        if (now != last_tick) {
            lv_tick_inc(now - last_tick);
            last_tick = now;
        }
#endif

        /* LVGL 任务处理, 每 5ms 一次 */
        if (now - last_lv_handler >= 5) {
            lv_timer_handler();
            last_lv_handler = now;
        }

        /* 应用状态机轮询, 每 50ms 一次 */
        thermo_app_poll();
        usleep(50000);
    }

    return 0;
}