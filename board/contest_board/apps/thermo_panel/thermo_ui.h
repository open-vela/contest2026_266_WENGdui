/**
 * @file thermo_ui.h
 * @brief 温控 UI 集成模块
 *
 * 提供温控主页/设置页/告警弹窗的创建与更新接口
 * 与 xiaozhi_ui.c 协作, 共享 LVGL 对象
 *
 * 【移植说明】本文件照抄厂家 app/src/thermo_ui.h。
 *   唯一改动: `#include "lv_obj.h"` -> `#include <lvgl.h>`
 *   (厂家 LVGL 的 include 布局与本仓库不同, 只改这一行包含方式,
 *    其余声明/宏全部保持厂家原文)。
 */
#ifndef THERMO_UI_H
#define THERMO_UI_H

#include <lvgl.h>
#include "thermo_app.h"

/* ---- 页面 ID ---- */
#define THERMO_PAGE_MAIN     0
#define THERMO_PAGE_SETTING  1

/* ---- 环境温度哨兵 ----
 * 本板只接 1 路 NTC (ch0/PA28 腔体), 无第二路环境传感器 -> 环境温度没有真实
 * 来源。用此哨兵值告知 UI 显示 "--", 不伪造数据 (见 thermo_ui.c 的 update_temp)。 */
#define THERMO_AMBIENT_NA    (-999.0f)

/* ---- 生命周期 ---- */
void thermo_ui_init(void);               /* 创建温控页面 */
void thermo_ui_show_page(int page);      /* 页面切换 */

/* ---- 数据更新 (thermo_app_poll 调用) ---- */
void thermo_ui_update_state(thermo_state_t s);
void thermo_ui_update_temp(float chamber, float ambient);
void thermo_ui_update_pid(float duty);
void thermo_ui_show_alarm(fault_mask_t f);
void thermo_ui_hide_alarm(void);

/* ---- 页面对象 (供 xiaozhi_ui.c 访问) ---- */
extern lv_obj_t *thermo_main_screen;
extern lv_obj_t *thermo_setting_screen;

#endif /* THERMO_UI_H */