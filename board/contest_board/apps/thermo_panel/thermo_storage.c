/**
 * @file thermo_storage.c
 * @brief Flash 参数存储 (NuttX MTD/FTL 接口)
 */
#include "thermo_app.h"
#include <nuttx/config.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define STORAGE_PATH "/dev/param0"
#define PARAMS_VERSION 1

static const thermo_params_t s_default = {
    .version = PARAMS_VERSION,
    .target_temp = 55.0f, .temp_high_limit = 80.0f, .temp_low_limit = 60.0f,
    .idle_timeout_s = 30, .timer_on_min = 0, .timer_off_min = 0,
    .fan_min_duty = 30, .hysteresis = 5, .run_hours = 0, .crc32 = 0,
};

static uint32_t crc32_calc(const uint8_t *data, uint32_t len) {
    static const uint32_t t[16] = {
        0,0x1DB71064,0x3B0E6E48,0x26D930AC,0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
        0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C,
    };
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ t[crc & 0xF];
        crc = (crc >> 4) ^ t[crc & 0xF];
    }
    return crc ^ 0xFFFFFFFF;
}

void thermo_storage_load(thermo_params_t *p) {
    int fd = open(STORAGE_PATH, O_RDONLY);
    if (fd < 0) goto def;
    thermo_params_t buf;
    int n = read(fd, &buf, sizeof(buf));
    close(fd);
    if (n != sizeof(buf)) goto def;
    if (buf.version != PARAMS_VERSION) goto def;
    uint32_t saved = buf.crc32; buf.crc32 = 0;
    if (crc32_calc((uint8_t*)&buf, sizeof(buf)) != saved) goto def;
    memcpy(p, &buf, sizeof(buf));
    p->crc32 = saved;
    printf("[THERMO] storage load OK\n");
    return;
def:
    memcpy(p, &s_default, sizeof(s_default));
    thermo_storage_save(p);
    printf("[THERMO] storage use default\n");
}

void thermo_storage_save(const thermo_params_t *p) {
    /* NuttX 字符/块设备不支持 O_CREAT, 仅用 O_WRONLY | O_TRUNC */
    int fd = open(STORAGE_PATH, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        printf("[THERMO] WARN: storage save open failed (fd=%d)\n", fd);
        return;
    }
    thermo_params_t buf;
    memcpy(&buf, p, sizeof(buf));
    buf.crc32 = 0;
    buf.crc32 = crc32_calc((uint8_t*)&buf, sizeof(buf));
    write(fd, &buf, sizeof(buf));
    close(fd);
    printf("[THERMO] storage save OK\n");
}
