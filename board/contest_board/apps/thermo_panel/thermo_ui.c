/**
 * @file thermo_ui.c
 * @brief 温控 UI 集成实现 — 深色模式 Apple iOS 风格 (LVGL 9.2)
 *
 * 设计语言:
 *   - 深色渐变背景 (黑 -> 深灰), 模拟 iOS 深色模式毛玻璃卡片
 *   - 大圆角深色卡片 + 细白描边 + 柔光, 边缘高光
 *   - iOS 深色模式强调色: 蓝 #0A84FF / 红 #FF453A / 绿 #30D158
 *   - 底部药丸形按钮 dock
 *
 * 主页布局 (竖屏, 宽高自适应):
 *   ┌─────────────────────┐
 *   │      ● 待机 ●         │  ← 状态胶囊
 *   │  ┌─────────────────┐ │
 *   │  │   25.6  °C      │ │  ← 当前温度卡片
 *   │  │  环境 22.1°C    │ │
 *   │  │  目标 55.0°C ⚙  │ │
 *   │  └─────────────────┘ │
 *   │  PID  ▓▓▓▓░░░░  65% │  ← 输出卡片
 *   │  ( − )  (电源)  ( + ) │  ← 药丸按钮
 *   └─────────────────────┘
 */
#include "thermo_ui.h"
#include "thermo_app.h"
#include "lvgl.h"
#include "rtthread.h"

/* 复用 xiaozhi_ui.c 中从 TTF 动态生成的字体 */
extern lv_font_t *font_medium;
extern const unsigned char xiaozhi_font[];
extern const int xiaozhi_font_size;

static lv_font_t *thermo_big_font = NULL;   /* 大号温度字体 (TTF 生成) */
static lv_font_t *thermo_title_font = NULL; /* 卡片标题字体 */
static lv_font_t *thermo_small_font = NULL; /* 小号说明字体 */

/* ---- iOS 深色模式配色 ---- */
#define IOS_BLUE     lv_color_hex(0x0A84FF)
#define IOS_RED      lv_color_hex(0xFF453A)
#define IOS_GREEN    lv_color_hex(0x30D158)
#define IOS_YELLOW   lv_color_hex(0xFFD60A)
#define IOS_LABEL    lv_color_hex(0xFFFFFF)   /* 主文本 (白)     */
#define IOS_LABEL2   lv_color_hex(0x8E8E93)   /* 次文本 (灰)     */
#define IOS_LABEL3   lv_color_hex(0xAEAEB2)   /* 三级文本        */
#define IOS_CARD_BG  lv_color_hex(0x1C1C1E)   /* 卡片背景        */
#define IOS_CARD_BR  lv_color_hex(0xFFFFFF)   /* 卡片描边 (低 alpha 用) */
#define IOS_BTN_BG   lv_color_hex(0x2C2C2E)   /* 次要按钮背景    */
#define IOS_SL_TRACK lv_color_hex(0x3A3A3C)   /* 滑动条轨道      */
#define IOS_SL_KNOB  lv_color_hex(0x636366)   /* 滑动条触摸环描边  */
#define IOS_BG_TOP   lv_color_hex(0x000000)   /* 背景渐变顶(黑)  */
#define IOS_BG_BOT   lv_color_hex(0x1C1C1E)   /* 背景渐变底(深灰) */

/* ---- 一位小数格式 (适配: 本板 NuttX 未开 CONFIG_LIBC_FLOATINGPOINT,
 *    %.1f 不会真正格式化浮点, 故用整数拼装一位小数, 与 %.1f 四舍五入一致) ---- */
static void fmt_1dp(char *buf, size_t n, float v)
{
    int  neg = 0;
    long t;

    if (v < 0.0f) { neg = 1; v = -v; }
    t = (long)(v * 10.0f + 0.5f);
    snprintf(buf, n, "%s%ld.%ld", neg ? "-" : "", t / 10, t % 10);
}

/* ---- 主页控件 ---- */
lv_obj_t *thermo_main_screen = NULL;
lv_obj_t *thermo_setting_screen = NULL;

static lv_obj_t *lbl_state = NULL;
static lv_obj_t *lbl_temp_big = NULL;
static lv_obj_t *lbl_temp_unit = NULL;
static lv_obj_t *lbl_ambient = NULL;
static lv_obj_t *lbl_target = NULL;
static lv_obj_t *bar_pid = NULL;
static lv_obj_t *lbl_pid_pct = NULL;
static lv_obj_t *alarm_overlay = NULL;
static lv_obj_t *alarm_box = NULL;
static lv_obj_t *alarm_title = NULL;
static lv_obj_t *alarm_text = NULL;

/* ---- 设置页控件 ---- */
static lv_obj_t *sl_target = NULL;
static lv_obj_t *sl_high = NULL;
static lv_obj_t *lbl_target_v = NULL;
static lv_obj_t *lbl_high_v = NULL;

/* ================================================================ */
/* 通用样式辅助                                                       */
/* ================================================================ */

/* 生成一个 iOS 深色毛玻璃卡片 (深底 + 细白描边 + 柔光) */
static void style_ios_card(lv_obj_t *obj, int radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, IOS_CARD_BG, 0);
    lv_obj_set_style_bg_opa(obj, 248, 0);      /* ~97% 深色 */
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, IOS_CARD_BR, 0);
    lv_obj_set_style_border_opa(obj, 26, 0);   /* ~10% 白描边 */
    /* 柔光 (映射到边缘, 形成柔和浮起) */
    lv_obj_set_style_shadow_width(obj, 16, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(obj, 120, 0);
    lv_obj_set_style_shadow_offset_y(obj, 6, 0);
}

/* 生成一个药丸形 iOS 按钮 (只做圆角/去边框, 颜色由调用方设置) */
static lv_obj_t *style_ios_pill(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 6, 0);
    lv_obj_set_style_shadow_opa(obj, 80, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_offset_y(obj, 2, 0);
    return obj;
}

static void label_on(lv_obj_t *lbl, lv_color_t color, lv_font_t *font)
{
    lv_obj_set_style_text_color(lbl, color, 0);
    if (font) lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_letter_space(lbl, 0, 0);
}

/* ================================================================ */
/* 按键事件回调                                                      */
/* ================================================================ */
static void btn_power_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_POWER);
}

static void btn_up_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_TEMP_UP);
}

static void btn_down_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_TEMP_DOWN);
}

static void btn_gear_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_ENTER_SETTING);
}

static void btn_alarm_confirm_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_ALARM_ACK);
    thermo_ui_hide_alarm();
}

static void btn_setting_ret_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_EXIT_SETTING);
}

static void btn_setting_save_cb(lv_event_t *e)
{
    (void)e;
    thermo_mark_activity();
    thermo_event(THERMO_EV_SAVE_PARAMS);
    thermo_event(THERMO_EV_EXIT_SETTING);
}

static void slider_target_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    g_thermo_params.target_temp = v * 0.5f;
    thermo_mark_activity();
    char buf[16], num[16];
    fmt_1dp(num, sizeof(num), g_thermo_params.target_temp);
    rt_snprintf(buf, sizeof(buf), "%s°C", num);
    lv_label_set_text(lbl_target_v, buf);
}

static void slider_high_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    g_thermo_params.temp_high_limit = v * 1.0f + 30.0f;
    thermo_mark_activity();
    char buf[16], num[16];
    fmt_1dp(num, sizeof(num), g_thermo_params.temp_high_limit);
    rt_snprintf(buf, sizeof(buf), "%s°C", num);
    lv_label_set_text(lbl_high_v, buf);
}

/* 设置 iOS 深色滑动条外观 */
static void style_ios_slider(lv_obj_t *sl)
{
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, IOS_SL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_height(sl, 8, LV_PART_MAIN);

    lv_obj_set_style_bg_color(sl, IOS_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_color(sl, IOS_SL_KNOB, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 1, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sl, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(sl, LV_OPA_30, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 0, LV_PART_KNOB);
}

/* ================================================================ */
/* 创建主页                                                          */
/* ================================================================ */
static void create_main_page(void)
{
    thermo_main_screen = lv_obj_create(NULL);

    /* 深色渐变背景 (黑 -> 深灰) */
    lv_obj_set_style_bg_color(thermo_main_screen, IOS_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(thermo_main_screen, IOS_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(thermo_main_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(thermo_main_screen, 0, 0);
    lv_obj_set_style_bg_grad_stop(thermo_main_screen, 255, 0);
    lv_obj_set_style_pad_all(thermo_main_screen, 0, 0);

    /* 字体创建 */
    if (!thermo_big_font && xiaozhi_font && xiaozhi_font_size > 0) {
        thermo_big_font   = lv_tiny_ttf_create_data(xiaozhi_font, xiaozhi_font_size, 46);
        thermo_title_font = lv_tiny_ttf_create_data(xiaozhi_font, xiaozhi_font_size, 17);
        thermo_small_font = lv_tiny_ttf_create_data(xiaozhi_font, xiaozhi_font_size, 13);
    }

    /* ---- 状态胶囊 ---- */
    lbl_state = lv_label_create(thermo_main_screen);
    lv_obj_set_style_text_color(lbl_state, IOS_LABEL2, 0);
    lv_obj_set_style_text_font(lbl_state, thermo_small_font ? thermo_small_font : font_medium, 0);
    lv_label_set_text(lbl_state, "● 待机");
    lv_obj_align(lbl_state, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_letter_space(lbl_state, 1, 0);

    /* ---- 当前温度卡片 ---- */
    lv_obj_t *temp_card = lv_obj_create(thermo_main_screen);
    lv_obj_set_size(temp_card, 226, 150);
    style_ios_card(temp_card, 30);
    lv_obj_align(temp_card, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *title = lv_label_create(temp_card);
    label_on(title, IOS_LABEL3, thermo_title_font);
    lv_label_set_text(title, "当前温度");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

    /* 大号温度数字 */
    lbl_temp_big = lv_label_create(temp_card);
    if (thermo_big_font) {
        lv_obj_set_style_text_font(lbl_temp_big, thermo_big_font, 0);
    } else if (font_medium) {
        lv_obj_set_style_text_font(lbl_temp_big, font_medium, 0);
    }
    lv_obj_set_style_text_color(lbl_temp_big, IOS_LABEL, 0);
    lv_label_set_text(lbl_temp_big, "--.-");
    lv_obj_align(lbl_temp_big, LV_ALIGN_LEFT_MID, 24, -4);

    /* °C 单位 */
    lbl_temp_unit = lv_label_create(temp_card);
    label_on(lbl_temp_unit, IOS_LABEL3, thermo_title_font);
    lv_label_set_text(lbl_temp_unit, "°C");
    lv_obj_align(lbl_temp_unit, LV_ALIGN_LEFT_MID, 96, 14);

    /* 环境/目标 信息行 */
    lbl_ambient = lv_label_create(temp_card);
    label_on(lbl_ambient, IOS_LABEL2, thermo_small_font);
    lv_label_set_text(lbl_ambient, "环境  --.-°C");
    lv_obj_align_to(lbl_ambient, lbl_temp_big, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lbl_target = lv_label_create(temp_card);
    label_on(lbl_target, IOS_BLUE, thermo_small_font);
    lv_label_set_text(lbl_target, "目标  --.-°C   设置 ›");
    lv_obj_align_to(lbl_target, lbl_ambient, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_add_flag(lbl_target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_target, btn_gear_cb, LV_EVENT_CLICKED, NULL);

    /* ---- PID 输出卡片 ---- */
    lv_obj_t *pid_card = lv_obj_create(thermo_main_screen);
    lv_obj_set_size(pid_card, 226, 62);
    style_ios_card(pid_card, 22);
    lv_obj_align(pid_card, LV_ALIGN_TOP_MID, 0, 200);

    lv_obj_t *pid_title = lv_label_create(pid_card);
    label_on(pid_title, IOS_LABEL, thermo_title_font);
    lv_label_set_text(pid_title, "加热输出");
    lv_obj_align(pid_title, LV_ALIGN_TOP_LEFT, 16, 10);

    lbl_pid_pct = lv_label_create(pid_card);
    label_on(lbl_pid_pct, IOS_BLUE, thermo_title_font);
    lv_label_set_text(lbl_pid_pct, "0%");
    lv_obj_align(lbl_pid_pct, LV_ALIGN_TOP_RIGHT, -16, 10);

    bar_pid = lv_bar_create(pid_card);
    lv_bar_set_range(bar_pid, 0, 100);
    lv_bar_set_value(bar_pid, 0, LV_ANIM_OFF);
    style_ios_slider(bar_pid);
    lv_obj_set_width(bar_pid, 194);
    lv_obj_align(bar_pid, LV_ALIGN_BOTTOM_MID, 0, -12);

    /* ---- 底部按钮 dock ---- */
    lv_obj_t *btn;
    lv_coord_t x = -78;

    /* 按键 - */
    btn = style_ios_pill(lv_button_create(thermo_main_screen));
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_bg_color(btn, IOS_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn, 250, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, IOS_CARD_BR, 0);
    lv_obj_set_style_border_opa(btn, 18, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x, -20);
    lv_obj_add_event_cb(btn, btn_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    label_on(lbl, IOS_BLUE, font_medium);
    lv_label_set_text(lbl, "−");
    lv_obj_center(lbl);
    x += 78;

    /* 电源键 */
    btn = style_ios_pill(lv_button_create(thermo_main_screen));
    lv_obj_set_size(btn, 66, 66);
    lv_obj_set_style_bg_color(btn, IOS_GREEN, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x, -18);
    lv_obj_add_event_cb(btn, btn_power_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    label_on(lbl, lv_color_black(), thermo_title_font);
    lv_label_set_text(lbl, "开 / 关");
    lv_obj_center(lbl);
    x += 78;

    /* 按键 + */
    btn = style_ios_pill(lv_button_create(thermo_main_screen));
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_bg_color(btn, IOS_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn, 250, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, IOS_CARD_BR, 0);
    lv_obj_set_style_border_opa(btn, 18, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x, -20);
    lv_obj_add_event_cb(btn, btn_up_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    label_on(lbl, IOS_BLUE, font_medium);
    lv_label_set_text(lbl, "+");
    lv_obj_center(lbl);
}

/* ================================================================ */
/* 创建设置页                                                        */
/* ================================================================ */
static void create_setting_page(void)
{
    thermo_setting_screen = lv_obj_create(NULL);

    /* 与主页一致的深色渐变背景 */
    lv_obj_set_style_bg_color(thermo_setting_screen, IOS_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(thermo_setting_screen, IOS_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(thermo_setting_screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(thermo_setting_screen, 0, 0);
    lv_obj_set_style_bg_grad_stop(thermo_setting_screen, 255, 0);
    lv_obj_set_style_pad_all(thermo_setting_screen, 0, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(thermo_setting_screen);
    label_on(title, IOS_LABEL, thermo_title_font);
    lv_label_set_text(title, "温度设置");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- 目标温度 设置行 ---- */
    lv_obj_t *row = lv_obj_create(thermo_setting_screen);
    lv_obj_set_size(row, 226, 96);
    style_ios_card(row, 22);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *t = lv_label_create(row);
    label_on(t, IOS_LABEL, thermo_title_font);
    lv_label_set_text(t, "目标温度");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);

    lbl_target_v = lv_label_create(row);
    label_on(lbl_target_v, IOS_BLUE, thermo_title_font);
    lv_label_set_text(lbl_target_v, "55.0°C");
    lv_obj_set_style_text_font(lbl_target_v, thermo_title_font, 0);
    lv_obj_align(lbl_target_v, LV_ALIGN_TOP_RIGHT, -16, 12);

    sl_target = lv_slider_create(row);
    lv_slider_set_range(sl_target, 0, 200);
    lv_slider_set_value(sl_target, 110, LV_ANIM_OFF);
    style_ios_slider(sl_target);
    lv_obj_set_width(sl_target, 194);
    lv_obj_align(sl_target, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(sl_target, slider_target_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 超温上限 设置行 ---- */
    row = lv_obj_create(thermo_setting_screen);
    lv_obj_set_size(row, 226, 96);
    style_ios_card(row, 22);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 162);

    t = lv_label_create(row);
    label_on(t, IOS_LABEL, thermo_title_font);
    lv_label_set_text(t, "超温上限");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);

    lbl_high_v = lv_label_create(row);
    label_on(lbl_high_v, IOS_BLUE, thermo_title_font);
    lv_label_set_text(lbl_high_v, "80.0°C");
    lv_obj_set_style_text_font(lbl_high_v, thermo_title_font, 0);
    lv_obj_align(lbl_high_v, LV_ALIGN_TOP_RIGHT, -16, 12);

    sl_high = lv_slider_create(row);
    lv_slider_set_range(sl_high, 0, 90);
    lv_slider_set_value(sl_high, 50, LV_ANIM_OFF);
    style_ios_slider(sl_high);
    lv_obj_set_width(sl_high, 194);
    lv_obj_align(sl_high, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(sl_high, slider_high_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 返回 / 保存 按钮 ---- */
    lv_obj_t *btn_ret = style_ios_pill(lv_button_create(thermo_setting_screen));
    lv_obj_set_size(btn_ret, 100, 40);
    lv_obj_set_style_bg_color(btn_ret, IOS_BTN_BG, 0);
    lv_obj_set_style_border_width(btn_ret, 1, 0);
    lv_obj_set_style_border_color(btn_ret, IOS_CARD_BR, 0);
    lv_obj_set_style_border_opa(btn_ret, 18, 0);
    lv_obj_align(btn_ret, LV_ALIGN_BOTTOM_LEFT, 18, -18);
    lv_obj_add_event_cb(btn_ret, btn_setting_ret_cb, LV_EVENT_CLICKED, NULL);
    t = lv_label_create(btn_ret);
    label_on(t, IOS_LABEL, thermo_title_font);
    lv_label_set_text(t, "返回");
    lv_obj_center(t);

    lv_obj_t *btn_save = style_ios_pill(lv_button_create(thermo_setting_screen));
    lv_obj_set_size(btn_save, 100, 40);
    lv_obj_set_style_bg_color(btn_save, IOS_BLUE, 0);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -18, -18);
    lv_obj_add_event_cb(btn_save, btn_setting_save_cb, LV_EVENT_CLICKED, NULL);
    t = lv_label_create(btn_save);
    label_on(t, lv_color_white(), thermo_title_font);
    lv_label_set_text(t, "保存");
    lv_obj_center(t);
}

/* ================================================================ */
/* 创建告警弹窗 (iOS 深色弹窗风格)                                     */
/* ================================================================ */
static void create_alarm_overlay(void)
{
    alarm_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_style_bg_color(alarm_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(alarm_overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(alarm_overlay, 0, 0);
    lv_obj_set_style_pad_all(alarm_overlay, 0, 0);
    lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    alarm_box = lv_obj_create(alarm_overlay);
    lv_obj_set_size(alarm_box, 210, 170);
    style_ios_card(alarm_box, 28);
    lv_obj_center(alarm_box);

    alarm_title = lv_label_create(alarm_box);
    label_on(alarm_title, IOS_RED, thermo_title_font);
    lv_label_set_text(alarm_title, "温度超限");
    lv_obj_align(alarm_title, LV_ALIGN_TOP_MID, 0, 16);

    alarm_text = lv_label_create(alarm_box);
    label_on(alarm_text, IOS_LABEL2, thermo_small_font);
    lv_label_set_long_mode(alarm_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(alarm_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(alarm_text, 170);
    lv_label_set_text(alarm_text, "腔体温度超出上限\n已断开加热负载\n请检查散热/负载");
    lv_obj_align(alarm_text, LV_ALIGN_CENTER, 0, 2);

    lv_obj_t *btn = style_ios_pill(lv_button_create(alarm_box));
    lv_obj_set_size(btn, 150, 36);
    lv_obj_set_style_bg_color(btn, IOS_RED, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(btn, btn_alarm_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    label_on(lbl, lv_color_white(), thermo_title_font);
    lv_label_set_text(lbl, "确定");
    lv_obj_center(lbl);
}

/* ================================================================ */
/* 页面切换                                                          */
/* ================================================================ */
void thermo_ui_show_page(int page)
{
    if (page == THERMO_PAGE_MAIN) {
        if (thermo_main_screen) {
            rt_kprintf("[THERMO] show_page MAIN, screen=%p\n", thermo_main_screen);
            lv_scr_load(thermo_main_screen);
            rt_kprintf("[THERMO] lv_scr_load done, active=%p\n", lv_screen_active());
        } else {
            rt_kprintf("[THERMO] ERROR: thermo_main_screen is NULL!\n");
        }
    } else if (page == THERMO_PAGE_SETTING) {
        if (thermo_setting_screen) lv_scr_load(thermo_setting_screen);
    }
}

/* ================================================================ */
/* 数据更新                                                          */
/* ================================================================ */
void thermo_ui_update_state(thermo_state_t s)
{
    if (!lbl_state) return;
    static const char *names[] = {
        "启动", "待机", "加热中", "恒温", "告警", "休眠"
    };
    const char *n = (s <= THERMO_STATE_SLEEP) ? names[s] : "?";
    char buf[24];
    rt_snprintf(buf, sizeof(buf), "● %s", n ? n : "?");

    /* 运行态用蓝色圆点, 待机灰色 */
    lv_obj_set_style_text_color(lbl_state,
        (s == THERMO_STATE_HEATING || s == THERMO_STATE_MAINTAIN) ?
            IOS_BLUE : IOS_LABEL2, 0);
    lv_label_set_text(lbl_state, buf);
}

void thermo_ui_update_temp(float chamber, float ambient)
{
    if (!lbl_temp_big) return;
    char buf[16], num[16];

    fmt_1dp(num, sizeof(num), chamber);
    lv_label_set_text(lbl_temp_big, num);

    if (lbl_ambient) {
        /* 本板无环境传感器 -> ambient 为哨兵 -999 时显示 "--" (不伪造数据) */
        if (ambient <= -100.0f) {
            lv_label_set_text(lbl_ambient, "环境  --");
        } else {
            fmt_1dp(num, sizeof(num), ambient);
            rt_snprintf(buf, sizeof(buf), "环境  %s°C", num);
            lv_label_set_text(lbl_ambient, buf);
        }
    }

    if (lbl_target) {
        fmt_1dp(num, sizeof(num), g_thermo_params.target_temp);
        rt_snprintf(buf, sizeof(buf), "目标  %s°C   设置 ›", num);
        lv_label_set_text(lbl_target, buf);
    }
}

void thermo_ui_update_pid(float duty)
{
    if (!bar_pid) return;
    int v = (int)(duty + 0.5f);
    if (v < 0) v = 0; if (v > 100) v = 100;
    lv_bar_set_value(bar_pid, v, LV_ANIM_OFF);

    if (lbl_pid_pct) {
        char buf[8];
        rt_snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(lbl_pid_pct, buf);
    }
}

void thermo_ui_show_alarm(fault_mask_t f)
{
    if (!alarm_overlay) return;
    lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    if (f & FAULT_OVER_TEMP) {
        lv_label_set_text(alarm_title, "温度超限");
        lv_label_set_text(alarm_text, "腔体温度超出上限\n已断开加热负载\n请检查散热/负载");
    } else if (f & FAULT_SENSOR_BREAK) {
        lv_label_set_text(alarm_title, "传感器断线");
        lv_label_set_text(alarm_text, "NTC 传感器断线\n已停止控温\n请检查接线");
    } else {
        lv_label_set_text(alarm_title, "采样异常");
        lv_label_set_text(alarm_text, "ADC 采样通道异常\n请重启或检查供电");
    }
}

void thermo_ui_hide_alarm(void)
{
    if (alarm_overlay) {
        lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ================================================================ */
/* 初始化                                                            */
/* ================================================================ */
void thermo_ui_init(void)
{
    create_main_page();
    create_setting_page();
    create_alarm_overlay();

    /* 更新初始显示 (适配: 环境无真实来源 -> 哨兵) */
    thermo_ui_update_temp(25.0f, THERMO_AMBIENT_NA);
    thermo_ui_update_pid(0.0f);
    thermo_ui_update_state(THERMO_STATE_IDLE);

    rt_kprintf("[THERMO] UI init done, main_screen=%p, setting_screen=%p\n",
               thermo_main_screen, thermo_setting_screen);
}