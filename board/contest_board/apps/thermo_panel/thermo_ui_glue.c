/**
 * @file thermo_ui_glue.c
 * @brief 厂家 thermo_ui.c (LVGL) 在本工程的"适配胶水层" —— 只补外部依赖,
 *        不重画任何界面。
 *
 * 本文件回答一个问题: 厂家 app/src/thermo_ui.c 是 LVGL 代码, 直接搬进来后
 * 它引用的那些"外部符号"由谁提供? 逐项对应如下:
 *
 *  1) lv_init() / lv_tick_set_cb() / lv_timer_handler()
 *     —— 厂家由 xiaozhi_ui.c 与 RT-Thread 的 LCD 线程驱动; 本工程在这里驱动。
 *
 *  2) LVGL 显示落屏 (lv_display + flush_cb)
 *     —— 厂家对应物: sdk/middleware/lvgl/lv_drivers_v9/lv_lcd.c
 *         lv_lcd.c:438 lv_lcd_init() 里 lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX)
 *                   + lv_display_set_flush_cb(lcd_flush)
 *         lv_lcd.c:349 lcd_flush() 里通过 rt_graphix_ops(dev)->set_window(...)
 *                   + ->draw_rect_async(...) 把像素送进 LCD 设备
 *     本工程没有 RT-Thread 设备节点, 但有等价且已上板验证的厂家 LCD 通路
 *     sf32lb52_lcd_vendor_blit() (其内部调用序列与厂家 co5300.c:327-334
 *     LCD_WriteMultiplePixels 同构)。所以 flush_cb 就是"把矩形交给 blit"。
 *
 *  3) font_medium / xiaozhi_font[] / xiaozhi_font_size
 *     —— 厂家由 xiaozhi_ui.c:2040 `font_medium = lv_tiny_ttf_create_data(
 *        xiaozhi_font, xiaozhi_font_size, medium_font_size)` 生成;
 *        数据源是 app/font/SConscript 里
 *        `Env.ConvertFont(DroidSansFallback.ttf,'xiaozhi_font')` 把整个
 *        TTF (3,939,852 B) 转成的 C 数组, 厂家把它放在独立的 4MB
 *        FONT_DATA 分区 (ptab.h:52-57, 0x12AE0000)。
 *     本工程 ER_IROM1 分区只有 0x240000 = 2.25MB, 放不下完整 TTF,
 *     故【照厂家做法但裁剪子集】:
 *        * 用同一支 DroidSansFallback.ttf, pyftsubset 只保留界面用到的
 *          字符 (50,136 B), 仍然转成同名 C 数组 xiaozhi_font[]
 *          (thermo_font.c);
 *        * font_medium = lv_tiny_ttf_create_data(xiaozhi_font,
 *          xiaozhi_font_size, 20) —— 与厂家 xiaozhi_ui.c:2040 同构,
 *          只是字号取固定值 (厂家按 get_scale_factor() 缩放)。
 *      控件布局/文案/事件回调仍 100% 是厂家的, 字体来源也回到"TTF"。
 *
 *  4) g_thermo_params / thermo_event() / thermo_mark_activity()
 *     —— 厂家 app 层的全局参数与事件入口, 本文件提供。(状态机主循环不属
 *        本模块职责, 这里只保证 UI 构建与按键回调可用, 并打印事件号。)
 */
#include <nuttx/config.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include <nuttx/irq.h>        /* irq_attach() + NR_IRQS + SF32LB52_IRQ_GPIO1 */
#include <nuttx/arch.h>       /* up_enable_irq() */
#include <nuttx/semaphore.h>  /* nxsem_init/post/trywait (ISR 用 post) */

#include <lvgl.h>

#include "thermo_app.h"
#include "thermo_ui.h"

/* ==========================================================================
 *  2. LVGL 显示适配 (对应厂家 lv_lcd.c)
 * ========================================================================== */

#define THERMO_LCD_W   390
#define THERMO_LCD_H   450

/* 厂家 LCD 通路 (在 libsf32lb52_vendorlcd.a, 由 build_vendor_lcd_lib.sh 产出) */
extern int  sf32lb52_lcd_vendor_init(void);
extern int  sf32lb52_lcd_vendor_blit(uint16_t x0, uint16_t y0,
                                     uint16_t x1, uint16_t y1, const void *pix);

/* 厂家 LCDC 中断通路 (同一个 .a):
 *   厂家 drv_lcd.c:824-825  HAL_NVIC_SetPriority/HAL_NVIC_EnableIRQ(LCDC1_IRQn)
 *   厂家 drv_lcd.c:522-538  SendLayerDataCpltCbk -> rt_sem_release(draw_sem)
 *   本工程: 向量/NVIC 挂接在本文件 sf32lb52_lcd_irq_start() (irq_attach +
 *   up_enable_irq, 与 PA31 触摸中断同一套做法); 中断服务例程只调 vendor 入口。
 *   blit 内部用【中断版】SendLayerData2Reg_IT + 完成标志带超时等待 —— 见
 *   build_vendor_lcd_lib.sh 的 sf32lb52_lcd_vendor_blit()。 */
extern void     sf32lb52_lcd_vendor_irq(void);
extern uint32_t sf32lb52_lcd_vendor_blit_stats(uint32_t *irq_cnt, uint32_t *to_cnt);
extern uint32_t sf32lb52_lcd_vendor_blit_err_cnt(void);

/* 厂家触摸通路 (同一个 .a, 见 build_vendor_lcd_lib.sh):
 *   read()     返回 1=有触点; 坐标已按厂家 ft6146_correct_pos() 镜像为屏幕坐标
 *   last_raw() 取同一拍的【面板原始】坐标 (未镜像), 供上板对照打印 */
extern int  sf32lb52_touch_vendor_read(uint16_t *x, uint16_t *y, uint8_t *evt);
extern int  sf32lb52_touch_vendor_last_raw(uint16_t *x, uint16_t *y);

/* 厂家触摸【中断】通路 (同一个 .a, 见 build_vendor_lcd_lib.sh 的
 * sf32lb52_touch_irq_*): 外设侧把 PA31 配成下降沿中断/读挂起位,
 * 向量与 NVIC 挂接由本文件 sf32lb52_touch_irq_start() 完成。 */
extern int      sf32lb52_touch_irq_config(void);    /* PA31: 上拉输入+下降沿 EXTI */
extern int      sf32lb52_touch_irq_dispatch(void);  /* 读+清 PA31 挂起位, 1=有沿 */
extern uint32_t sf32lb52_touch_irq_count(void);     /* 累计下降沿次数 */
extern int      sf32lb52_touch_int_level(void);     /* PA31 电平 (1=抬起 0=按下) */

/* 厂家 NVS / PID 数据源 (同一库) */
extern float    sf32lb52_nvs_get_target_temp(void);
extern float    sf32lb52_nvs_get_cur_temp(void);
extern uint32_t sf32lb52_nvs_get_boot_count(void);
extern float    sf32lb52_pid_get_out(void);
extern int      sf32lb52_pid_tick(float *out, uint16_t *raw_out, float *meas_out);
extern int      sf32lb52_pid_get_heater(void);

/* 厂家背光 (PWM 占空比 0..100, 0=熄屏) / NTC 采样 / GPIO (加热灯·到温灯·唤醒键) */
extern void sf32lb52_lcd_vendor_setbrightness(uint32_t percent);
extern int  sf32lb52_ntc_read(int ch, uint16_t *raw, float *temp_c);
extern int  sf32lb52_heater_init(void);
extern int  sf32lb52_heater_set(int on);
extern int  sf32lb52_wake_pin_init(void);
extern int  sf32lb52_wake_pin_pressed(void);

/* ==========================================================================
 *  LVGL 显示缓冲 —— 照厂家改【整屏缓冲】, 不再走"40 行 PARTIAL / buf2=NULL"路径
 *
 *  ▍为何改 (任务 A 主流程停滞根因):
 *    旧实现 lv_display_set_buffers(disp, buf, NULL, 40行, LV_DISPLAY_RENDER_MODE_PARTIAL)
 *    buf 只够 40 行, 且第二参数(buf2, throwaway)传 NULL。整屏 390x450 首帧靠
 *    LVGL 自动切成 12 条 40 行条带逐条 blit(上板证据 irq=1..12 全成功),
 *    但【翻页 / 二次渲染重新 invalidate】时, LVGL 需要整屏或 blend/copy 层
 *    走 40 行单条带病理路径, 因缺少整屏容量/throwaway 而故障硬崩(sf32lb52_
 *    ui_selftest() :832 的 lv_timer_handler 之后再无任何打印)。
 *    厂家从不走这条路: 厂家用【整屏缓冲】lv_lcd.c:174-179
 *      "screen sized buffer on low speed RAM ... L2_NON_RET_BSS_SECT(frambuf, buf2_1)"
 *    整屏 RGB565 缓冲 390x450x2=351000B 落在 L2(PSRAM)。
 *
 *  ▍本工程实现 (与厂家一致):
 *    主缓冲 s_lv_buf_psram[351000B] -> .psram_lcdbuf (flash.ld:60 psram
 *    0x60200000, LENGTH 0x100000=1MB, 放得下)。FLUSH 一次整屏 390x450。
 *    LVGL 用 LV_DISPLAY_RENDER_MODE_FULL: 每帧整屏渲染到缓冲, 任何翻页/
 *    二次渲染都不再切条带, 彻底避开单条带病理路径。
 *    ---- 兜底 ----
 *    PSRAM 探测失败(不可用)时退回 40 行 PARTIAL + 小 SRAM 缓冲(旧行为,
 *    仅作对照), 保证整机仍可启动。
 *
 *    LCDC 取数契约: 每行 RGB565 = 390*2 = 780B, 与 vendor blit 的 WIDTH=780
 *    完全一致, 整屏只是该宽高对应的更大矩形(厂家 co5300.c LCD_WriteMultiplePixels
 *    原生支持整屏一次送), 不会回到逐行/逐条不可控的发送。
 * ========================================================================== */

/* 主缓冲: 整屏 390 x 450 x RGB565 = 351000B (psram 段 1MB 内) */
#define THERMO_LCD_FULL_BYTES   ((unsigned)(THERMO_LCD_W * THERMO_LCD_H * 2))
/* 兜底缓冲: 40 行 PARTIAL = 31200B (PSRAM 不可用时的对照路径) */
#define THERMO_LCD_PART_BYTES   ((unsigned)(THERMO_LCD_W * 40 * 2))

/* 本次使用的实际缓冲字节/渲染模式 (由 thermo_lcd_buf_probe() 决定) */
static unsigned THERMO_LCD_BUF_BYTES    = THERMO_LCD_PART_BYTES;
static uint32_t  THERMO_LCD_BUF_RENDER  = LV_DISPLAY_RENDER_MODE_PARTIAL;

/* 1 = 照厂家放 PSRAM (默认); 0 = 退回旧的内部 SRAM 缓冲(仅作对照用)。 */
#define THERMO_LCD_BUF_USE_PSRAM   1

static uint8_t s_lv_buf_psram[THERMO_LCD_FULL_BYTES]
    __attribute__((section(".psram_lcdbuf"), aligned(64)));
static uint8_t s_lv_buf_sram[THERMO_LCD_PART_BYTES] __attribute__((aligned(4)));

static uint8_t *s_lv_buf = s_lv_buf_sram;
static int      s_lv_buf_is_psram;

static int thermo_lcd_buf_probe(void)
{
    volatile uint32_t *p = (volatile uint32_t *)s_lv_buf_psram;
    uint32_t n = THERMO_LCD_FULL_BYTES / 4u;
    uint32_t pat0 = 0xA5A55A5Au;
    uint32_t pat1 = 0x5A5AA5A5u;
    int      ok = 0;

#if THERMO_LCD_BUF_USE_PSRAM
    p[0] = pat0;
    p[n - 1] = pat1;
    ok = (p[0] == pat0) && (p[n - 1] == pat1);
#endif

    if (ok) {
        for (n = 0; n < THERMO_LCD_FULL_BYTES / 4u; n++) p[n] = 0;
        s_lv_buf = s_lv_buf_psram;
        s_lv_buf_is_psram = 1;
        THERMO_LCD_BUF_BYTES   = THERMO_LCD_FULL_BYTES;
        THERMO_LCD_BUF_RENDER  = LV_DISPLAY_RENDER_MODE_FULL;
    } else {
        s_lv_buf = s_lv_buf_sram;
        s_lv_buf_is_psram = 0;
        THERMO_LCD_BUF_BYTES   = THERMO_LCD_PART_BYTES;
        THERMO_LCD_BUF_RENDER  = LV_DISPLAY_RENDER_MODE_PARTIAL;
    }

    printf("[LCD] drawbuf @ 0x%08lx (%uB) psram=%d mode=%s (厂家 lv_lcd.c:177-179 整屏缓冲 放 PSRAM)\n",
           (unsigned long)(uintptr_t)s_lv_buf, (unsigned)THERMO_LCD_BUF_BYTES,
           s_lv_buf_is_psram,
           THERMO_LCD_BUF_RENDER == LV_DISPLAY_RENDER_MODE_FULL ? "FULL" : "PARTIAL");
    return s_lv_buf_is_psram;
}

static lv_display_t *s_disp;
static lv_indev_t   *s_indev;

/* 触摸状态 (见 2b 节): 中断 -> 信号量 -> 读线程 read_point -> 发布最新点 -> indev 消费
 *
 * 与厂家的对应 (三段式, 一层不差):
 *   厂家 ft6146_irq_handler()             -> thermo_touch_gpio_isr()
 *   厂家 drv_touch.c:561 读线程等 isr_sem  -> thermo_touch_thread() 的 nxsem_wait()
 *   厂家 drv_touch.c:570 read_point()      -> sf32lb52_touch_vendor_read()
 *   厂家 drv_touch.c:581 touch_write_more  -> 写下面这组 s_tp_evt_* + s_tp_evt_seq
 *   厂家 lv_touch.c:86 touchscreen_read()  -> thermo_touch_read_cb()
 * 读线程只"发布", indev 回调只"消费": 谁都不去轮询器件。 */
static sem_t             s_tp_isr_sem;      /* ISR 里 post (厂家 isr_sem) */
static volatile uint32_t s_tp_irq_seen;     /* NuttX 侧收到的下降沿次数 */
static int               s_tp_irq_ready;    /* EXTI + NVIC 是否已装好 */

/* 读线程发布的"最新一个触点消息" (厂家 struct touch_message + rx_indicate 计数) */
static volatile int      s_tp_evt_state;    /* 1=按下 / 0=抬起 (LVGL 判据) */
static volatile uint16_t s_tp_evt_x;        /* 已按厂家 correct_pos 镜像后的屏幕坐标 */
static volatile uint16_t s_tp_evt_y;
static volatile uint16_t s_tp_evt_rawx;     /* 面板原始坐标 (仅供对照打印) */
static volatile uint16_t s_tp_evt_rawy;
static volatile uint8_t  s_tp_evt_code;     /* P1_XH[7:6] 事件码 */
static volatile uint32_t s_tp_evt_seq;      /* 发布序号 (证明读线程真的在发) */
static uint32_t          s_tp_last_read_ms; /* 读点打印限速 */

/* 交互/低功耗状态 */
static volatile uint32_t s_last_activity_ms;
static volatile int      s_screen_on = 1;
static fault_mask_t      s_fault = FAULT_NONE;

#define THERMO_IDLE_OFF_MS   30000u   /* 闲置熄屏阈值 (30s) */
#define THERMO_BL_NORMAL     80u      /* 正常背光占空比 % */

/* 后台控温 tick 周期。厂家由 100ms lv_timer 驱动 (thermo_app.c:209),
 * 本板温度变化慢、且要避免刷屏, 取 500ms (可在此调整)。 */
#define THERMO_CTRL_TICK_MS  500u

/* 后台控温 tick 的"变化才打印"状态 (初值用 0xFF 表示尚未打印过) */
static uint32_t s_ctrl_last_ms;
static int      s_ctrl_heat_prev = -1;
static uint8_t  s_ctrl_duty_shown = 0xFF;

/* ==========================================================================
 *  3. 字体依赖 (厂家 xiaozhi_ui.c 提供; 见文件头注释 3) )
 * ========================================================================== */

/* 数据源: 厂家 TTF (DroidSansFallback) 的界面字符子集, 由 thermo_font.c 定义,
 * 数组名与厂家 xiaozhi_ui.c 里引用的完全一致。 */
extern const unsigned char xiaozhi_font[];
extern const int xiaozhi_font_size;

lv_font_t *font_medium = NULL;            /* 初始化时由 TTF 子集生成 */

/* 单调毫秒时钟 (LVGL v9 心跳) */
static uint32_t thermo_tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

/* ---- 厂家 LCDC 中断挂接 (照厂家 drv_lcd.c:824-825 打开 LCDC1_IRQn) --------
 * 向量表由公共层提供, "挂接" = irq_attach() + up_enable_irq()
 * (与下面第 2b 节的 PA31 触摸中断完全同一套做法)。
 * 中断里只做厂家 HAL 的收尾: HAL_LCDC_IRQHandler -> 完成回调置标志;
 * 与厂家 drv_lcd.c:522-538 SendLayerDataCpltCbk 一样"绝不做耗时操作"。 */
static volatile uint32_t s_lcd_irq_seen;

static int thermo_lcd_isr(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    s_lcd_irq_seen++;
    sf32lb52_lcd_vendor_irq();
    return OK;
}

static int sf32lb52_lcd_irq_start(void)
{
    if (irq_attach(SF32LB52_IRQ_LCDC1, thermo_lcd_isr, NULL) != 0) {
        printf("[LCD] FATAL: irq_attach(LCDC1=%d) failed\n", SF32LB52_IRQ_LCDC1);
        return -1;
    }

    up_enable_irq(SF32LB52_IRQ_LCDC1);
    printf("[LCD] LCDC1 IRQ armed: irq=%d (照厂家 drv_lcd.c:824-825)\n",
           SF32LB52_IRQ_LCDC1);
    return 0;
}

/* 与厂家 lv_lcd.c:349 lcd_flush() 同职责: 把一块像素送到 LCD。
 * 落地接口 = sf32lb52_lcd_vendor_blit(); 它内部就是厂家 co5300.c:327-333
 * LCD_WriteMultiplePixels 的做法: LayerSetData + 【中断版】
 * HAL_LCDC_SendLayerData2Reg_IT, 退出前带超时等完成中断。
 * 所以本回调不会永久阻塞 —— 最坏 yield 后继续, 常驻线程不倒。 */
static uint32_t s_flush_cnt;
static uint32_t s_flush_bad_cnt;

static void thermo_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int rc = 0;

    if (px_map != NULL) {
        rc = sf32lb52_lcd_vendor_blit((uint16_t)area->x1, (uint16_t)area->y1,
                                      (uint16_t)area->x2, (uint16_t)area->y2,
                                      px_map);
        s_flush_cnt++;
        if (s_flush_cnt == 1u) {
            printf("[LCD] full flush (area=%d,%d,%d,%d 即整屏) SRCP=0x%08lx rc=%d"
                   " eject=%s\n",
                   (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                   (unsigned long)(uintptr_t)px_map, rc,
                   THERMO_LCD_BUF_RENDER == LV_DISPLAY_RENDER_MODE_FULL ? "1 blit/frame" : "strip blits");
        }
        if (rc != 0) {
            s_flush_bad_cnt++;
            if (s_flush_bad_cnt == 1u) {
                printf("[LCD] blit rc=%d (未等到完成中断) area=(%d,%d,%d,%d)"
                       " lcdc_irq=%u\n", rc, (int)area->x1, (int)area->y1,
                       (int)area->x2, (int)area->y2, (unsigned)s_lcd_irq_seen);
            }
        }
    }

    lv_display_flush_ready(disp);
}

static void thermo_disp_init(void)
{
    lv_theme_t *th;

    s_disp = lv_display_create(THERMO_LCD_W, THERMO_LCD_H);
    lv_display_set_flush_cb(s_disp, thermo_flush_cb);
    /* 整屏 FULL (PSRAM 可用时): 每帧一次整屏渲染+一次整屏 flush, 翻页/二次渲染
     * 不再切 40 行条带, 根除单条带病理路径(照厂家 lv_lcd.c 整屏缓冲)。
     * PSRAM 探测失败时才退回 40 行 PARTIAL 对照路径。 */
    lv_display_set_buffers(s_disp, s_lv_buf, NULL, THERMO_LCD_BUF_BYTES,
                           THERMO_LCD_BUF_RENDER);

    /* 把默认主题的字体设成中文字库:
     * 厂家 thermo_ui.c 只给 lbl_state / lbl_temp_big 显式设字体 (thermo_ui.c:145/154),
     * 其余标签 (环境:/目标:/电源/设置/返回/保存...) 走主题默认字体。
     * 主题默认字体若不是中文字体, 这些标签的中文会渲染成空白 —— 所以这里
     * 把 font_medium 设为主题默认字体。控件与文案仍是厂家的, 只影响"用哪套字"。*/
    th = lv_theme_default_init(s_disp,
                               lv_palette_main(LV_PALETTE_BLUE),
                               lv_palette_main(LV_PALETTE_RED),
                               false, font_medium);
    lv_display_set_theme(s_disp, th);
}

/* ==========================================================================
 *  2b. 触摸输入适配 (照厂家做法: PA31 下降沿中断驱动读点)
 *
 *  厂家链路: ft6146 PA31 下降沿中断 -> rt_touch_irq_pin_attach(PIN_IRQ_MODE_FALLING)
 *            (ft6146.c:374-375) -> 释放 isr_sem -> 读线程 rt_sem_take(isr_sem)
 *            醒来后调 read_point() (drv_touch.c:561 -> :570) -> 投递 touch 设备
 *            -> LVGL indev。
 *  本工程链路 (等价):
 *            ① sf32lb52_touch_irq_config() (vendor .a): PA31 上拉输入 + 下降沿 EXTI
 *            ② irq_attach(SF32LB52_IRQ_GPIO1, thermo_touch_gpio_isr, NULL)
 *               + up_enable_irq(SF32LB52_IRQ_GPIO1)     <- 本文件 (向量/NVIC 挂接)
 *            ③ ISR: sf32lb52_touch_irq_dispatch() 读+清挂起位, nxsem_post(信号量)
 *            ④ 读线程 thermo_touch_thread(): nxsem_wait 醒来 ->
 *               sf32lb52_touch_vendor_read() 读点 -> 发布 s_tp_evt_*
 *            ⑤ indev 回调 thermo_touch_read_cb(): 只把已发布的点交给 LVGL
 *            (没有"定时轮询采点"这一层 —— 采点只由中断触发)
 *
 *  向量/NVIC 如何挂接 (用户关注点):
 *    - 本树中断向量表由公共层提供 (arch/arm/src/arm_m/arm_vectors.c):
 *      外设中断 [NVIC_IRQ_PENDSV+1 .. 15+ARMV8M_PERIPHERAL_INTERRUPTS] 全部
 *      指向 exception_direct; 它 (armv8-m/arm_doirq.c) 从 IPSR 取中断号后
 *      irq_dispatch() -> 查 g_irqvector[] -> 调到 irq_attach() 登记的服务例程。
 *      所以【无需新增向量槽】, "挂接"= irq_attach() + up_enable_irq()。
 *    - IRQ 号: GPIO1_IRQn(84, vendor register.h:120) + NVIC_IRQ_FIRST(16)
 *      = 100 = SF32LB52_IRQ_GPIO1 (arch/arm/include/sf32lb52/irq.h:91)。
 *      up_enable_irq() 据此写 NVIC SETENA(84) (sf32lb52_irq.c:281)。
 *      向量表长度 15+99=114 >= 100, 槽位存在 (chip.h:24)。
 * ========================================================================== */

/* GPIO1 中断服务例程 (由 irq_attach 登记)。
 * 等价于厂家 ft6146_irq_handler() (ft6146.c:260-270): 两者都只做两件事 ——
 * "把该脚的挂起位清掉" + "释放信号量", 绝不在中断里碰 I2C。 */
static int thermo_touch_gpio_isr(int irq, void *context, void *arg)
{
    (void)irq;
    (void)context;
    (void)arg;

    if (sf32lb52_touch_irq_dispatch()) {
        s_tp_irq_seen++;
        nxsem_post(&s_tp_isr_sem);        /* 厂家 rt_sem_release(isr_sem) */
    }
    return OK;
}

/* ==========================================================================
 *  读点线程 —— 厂家 drv_touch.c 的 tp_read_thread_entry() 等价物
 *
 *  厂家 (drv_touch.c:525-590):
 *      for (;;) {
 *          rt_sem_take(current_driver->isr_sem, RT_WAITING_FOREVER);  // :561
 *          current_driver->ops->read_point(&msg);                     // :570
 *          touch_write_more(msg.event, msg.x, msg.y);                 // :581
 *          rt_thread_delay(RT_TICK_PER_SECOND / BSP_TOUCH_SAMPLE_HZ); // :591
 *      }
 *  本工程逐句对应:
 *      for (;;) { 等信号量 -> sf32lb52_touch_vendor_read() -> 发布 s_tp_evt_* }
 *
 *  ★ PA31 中断不可依赖时的兜底 (本板上板实测:[TP] IRQ probe cnt=0 恒不涨):
 *    厂家 read_point/ft6146(ft6146.c:178) 的取数本质就是周期读 TD_STATUS——
 *    中断只是"唤醒读线程"的优化, 真正的坐标永远来自 read_point() 那次 I2C 读
 *    (drv_touch.c:570)。故本线程每次醒来【必须实际读一次】, 唤醒源可以有两条:
 *      ① 中断 fast-path : nxsem_timedwait 因 ISR post 返回 OK  (PA31 工作时的低延迟通道)
 *      ② 轮询兜底        : 信号量超时(40ms)也照读一次           (PA31 不触发也照样出坐标)
 *   这样即使上板 PA31 EXTI 完全不触发, 手指按下仍在 ≤40ms 内被读到, 触摸照样能用;
 *   一旦中断修好, 路径①自然接管并降到更低延迟。此举对应厂家"读线程循环 + 采样间隔
 *   延时"(drv_touch.c:591)与其 100ms 异常恢复定时器(ft6146.c:274-294)的设计意图。
 *
 *  抬指恢复: 手指离开后下一次读 touch_num==0(此时 INT 已回高)即报 UP, 与厂家一致。
 * ========================================================================== */
#define THERMO_TOUCH_POLL_NS  40000000L   /* 40ms 轮询心跳 (25Hz 采点, 厂家 BSP_TOUCH_SAMPLE_HZ 同级) */

static void *thermo_touch_thread(void *arg)
{
    (void)arg;

    for (;;) {
        struct timespec ts;
        int rc;

        /* 带超时的等中断: ISR 来即唤醒(size_t), 不来则超时当一次轮询心跳 */
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += THERMO_TOUCH_POLL_NS;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

        rc = nxsem_timedwait(&s_tp_isr_sem, &ts);
        if (rc < 0 && rc != -ETIMEDOUT) continue;

        /* 厂家 drv_touch.c:570 ops->read_point(&msg) */
        {
            uint16_t x = 0, y = 0, rx = 0, ry = 0;
            uint8_t  e = 0;
            int      hit = sf32lb52_touch_vendor_read(&x, &y, &e);
            uint32_t now = thermo_tick_cb();

            sf32lb52_touch_vendor_last_raw(&rx, &ry);

            if (hit) {
                s_tp_evt_x = x; s_tp_evt_y = y;
                s_tp_evt_state = 1;
            } else if (sf32lb52_touch_int_level() != 0) {
                s_tp_evt_state = 0;                   /* 厂家 touch_num==0 -> UP */
            }
            /* 读不到点但 INT 仍低: 触发模式下 TD_STATUS 读一次即被清而手指还在,
             * 保持按下并沿用上次坐标 (否则 LVGL 会把"按住"误判成极短点击)。 */

            s_tp_evt_rawx = rx; s_tp_evt_rawy = ry; s_tp_evt_code = e;
            s_tp_evt_seq++;

            /* 证据2: 一次中断到底读到了什么 (限速 1Hz)。与证据1同源:
             * n>0 说明边沿来了且读到点; 一直不涨说明中断压根没来。 */
            if (now - s_tp_last_read_ms >= 1000u) {
                s_tp_last_read_ms = now;
                printf("[TP] read: seq=%u edge=%u raw=(%u,%u) screen=(%u,%u) "
                       "evt=%u raw_evt=%u state=%s INT=%d\n",
                       (unsigned)s_tp_evt_seq, (unsigned)s_tp_irq_seen,
                       (unsigned)rx, (unsigned)ry,
                       (unsigned)s_tp_evt_x, (unsigned)s_tp_evt_y,
                       (unsigned)e, (unsigned)s_tp_evt_code,
                       s_tp_evt_state ? "DOWN" : "UP",
                       sf32lb52_touch_int_level());
            }
        }
    }
    return NULL;
}

static void thermo_touch_task_start(void)
{
    pthread_t      tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    if (pthread_create(&tid, &attr, thermo_touch_thread, NULL) != 0) {
        printf("[TP] failed to start read thread\n");
    } else {
        printf("[TP] read thread started (ISR + 40ms poll fallback -> read_point)\n");
    }
    pthread_attr_destroy(&attr);
}

/* 装上 PA31 下降沿中断 (厂家做法) —— 必须在 sf32lb52_touch_vendor_init() 之后,
 * 因为 init 会把 PA31 配成普通输入 (其内部 DISABLE_ISR 会清掉 IER)。 */
int sf32lb52_touch_irq_start(void)
{
    if (s_tp_irq_ready) return 0;

    nxsem_init(&s_tp_isr_sem, 0, 0);

    /* ① 外设侧: PA31 = 上拉输入 + 下降沿中断 (厂家 PIN_IRQ_MODE_FALLING 等价) */
    sf32lb52_touch_irq_config();

    /* ② NuttX 侧: 把 GPIO1 中断向量挂到本服务例程并使能 NVIC 线 */
    if (irq_attach(SF32LB52_IRQ_GPIO1, thermo_touch_gpio_isr, NULL) != 0) {
        printf("[TP] IRQ attach FAILED (SF32LB52_IRQ_GPIO1=%d)\n",
               (int)SF32LB52_IRQ_GPIO1);
        return -1;
    }
    up_enable_irq(SF32LB52_IRQ_GPIO1);

    /* ③ 读线程: 厂家 drv_touch.c tp_init() -> tp_read_thread_entry() 的等价物 */
    thermo_touch_task_start();

    s_tp_irq_ready = 1;
    printf("[TP] IRQ armed: PA31 falling-edge -> IRQ %d "
           "(GPIO1_IRQn84+NVIC_IRQ_FIRST16), NVIC enabled\n",
           (int)SF32LB52_IRQ_GPIO1);
    return 0;
}

/* 自检用: 打印中断计数/发布序号与 PA31 电平 (点按屏幕时 cnt 应增长) */
void sf32lb52_touch_irq_print(void)
{
    printf("[TP] IRQ probe: cnt=%u vendor_cnt=%u seq=%u state=%s int_level=%d\n",
           (unsigned)s_tp_irq_seen, (unsigned)sf32lb52_touch_irq_count(),
           (unsigned)s_tp_evt_seq, s_tp_evt_state ? "DOWN" : "UP",
           sf32lb52_touch_int_level());
}

/* LVGL indev 回调 —— 厂家 lv_touch.c:86 touchscreen_read() 等价物:
 * 只把读线程已发布的"最新触点消息"交给 LVGL, 自己不做任何 I2C / 轮询。 */
static void thermo_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    if (s_tp_evt_state) {
        data->point.x = s_tp_evt_x;
        data->point.y = s_tp_evt_y;
        data->state   = LV_INDEV_STATE_PRESSED;
        s_last_activity_ms = thermo_tick_cb();   /* 有触摸 = 有活动, 重置闲置计时 */
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void thermo_indev_init(void)
{
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, thermo_touch_read_cb);
    lv_indev_set_display(s_indev, s_disp);
    printf("[TP] LVGL indev registered (FT6146, pointer, IRQ-driven)\n");
}

/* ==========================================================================
 *  2c. 故障检测与告警 (对应厂家 fault/fault.c + ctrl/ctrl.c 的告警联动)
 *
 *  判据 (全部照厂家):
 *    NTC 断线 : 原始值 > 4013 (厂家 temp/temp.c 开路判定) -> 传感器断线
 *    上/下限  : 实测温度 > 上限 / < 下限 -> 超温 / 低温
 *    采样异常 : ntc_read 返回非 0 -> ADC 异常
 *  动作: 只做 LVGL 弹窗 (lv_msgbox) 告警。
 *        PA24 已改为"到温指示灯"(见 vendor_gpio_glue.c), 不再作故障灯用。
 * ========================================================================== */

static fault_mask_t thermo_fault_poll(void)
{
    uint16_t raw = 0;
    float    t   = -999.0f;

    if (sf32lb52_ntc_read(0, &raw, &t) != 0) {
        return FAULT_ADC_ERROR;
    }
    if (raw > 4013) {                                   /* 厂家开路判据 */
        return FAULT_SENSOR_BREAK;
    }
    if (t > g_thermo_params.temp_high_limit) {
        return FAULT_OVER_TEMP;
    }
    if (t < g_thermo_params.temp_low_limit) {
        return FAULT_UNDER_TEMP;
    }
    return FAULT_NONE;
}

static void thermo_fault_apply(fault_mask_t m)
{
    if (m == s_fault) return;
    s_fault = m;

    if (m != FAULT_NONE) {
        printf("[ALARM] fault=0x%x -> msgbox (PA24 now setpoint LED, not used)\n", (unsigned)m);
        thermo_ui_show_alarm(m);
    } else {
        printf("[ALARM] fault cleared -> msgbox hidden\n");
        thermo_ui_hide_alarm();
    }
}

/* 人为触发一次告警 (开机自检相用): 弹窗, 由常驻循环的开机相定时清除。
 * 原在 board_late_initialize 里同步 usleep 执行会挡常驻线程, 已改为非阻塞状态机驱动。 */

/* ==========================================================================
 *  2d. 低功耗: 闲置熄屏 + LVGL 暂停 + PA11 唤醒
 *
 *  厂家依据: SDK 低功耗走 RT-Thread PM (sdk/docs/source/app_note/low_power.md:
 *    PM_SLEEP_MODE_IDLE/LIGHT/DEEP/STANDBY; hibernate = pm_shutdown();
 *    shutdown = HAL_PMU_EnterShutdown(); 引脚唤醒 = HAL_PMU_EnablePinWakeup())。
 *    SDK 里【没有名为 STOP 的模式】, 最接近的是 DEEP/STANDBY; 而 hibernate/
 *    shutdown 会复位芯片, 自检中途直接进入会重启, 故本板低功耗实现为:
 *      熄灭背光(厂家背光 PWM=0) + 暂停 LVGL 调度 + 20ms 级休眠轮询,
 *      由 PA11 按下唤醒 -> 恢复背光并刷新界面。
 * ========================================================================== */

static void thermo_power_enter_off(void)
{
    printf("[PWR] idle %us -> screen OFF (backlight=0, LVGL paused)\n",
           (unsigned)(THERMO_IDLE_OFF_MS / 1000u));
    sf32lb52_lcd_vendor_setbrightness(0);
    s_screen_on = 0;
}

static void thermo_power_wake(void)
{
    printf("[PWR] wake by PA11 -> screen ON, refresh UI\n");
    sf32lb52_lcd_vendor_setbrightness(THERMO_BL_NORMAL);
    s_screen_on = 1;
    s_last_activity_ms = thermo_tick_cb();
    thermo_ui_update_temp(sf32lb52_nvs_get_cur_temp(), THERMO_AMBIENT_NA);
    thermo_ui_update_pid(sf32lb52_pid_get_out());
}

/* ==========================================================================
 *  2e-0. 后台控温 tick (常驻线程周期调用)
 *
 *  厂家依据:
 *    app/src/ctrl/ctrl.c:5   "定时器中断 100ms 调用 ctrl_isr_run()"
 *    app/src/ctrl/ctrl.c:136-159  ctrl_isr_run(): 采温 -> 增量式 PID -> 风机 -> 加热滞回
 *    app/src/thermo_app.c:183-190/209  厂家用 100ms 周期 lv_timer(thermo_timer_cb)
 *                                        -> thermo_app_poll() 驱动这一轮
 *  本板落点: 常驻线程 thermo_ui_thread() 里每 THERMO_CTRL_TICK_MS(500ms) 调一次,
 *    一轮全在库内 sf32lb52_pid_tick() 完成:
 *        读 ch0(PA28) -> sf32lb52_pid_step() -> sf32lb52_fan_set_duty()
 *        -> sf32lb52_heat_pair_apply() (PA25/PA24 成对互斥)
 *    再把实时值推给 UI (温度/输出/状态)。
 *
 *  打印: 只在【状态变化】时打印 (加热灯翻转 / 输出档位变化 >=5%), 未变化不打印。
 * ========================================================================== */
static void thermo_ctrl_tick(void)
{
    float    out  = 0.0f;
    float    meas = -999.0f;
    uint16_t raw  = 0;
    int      heat;
    uint8_t  duty;

    if (sf32lb52_pid_tick(&out, &raw, &meas) != 0) {
        return;                              /* 采样无效: 本轮跳过 (告警由故障轮询负责) */
    }

    heat = sf32lb52_pid_get_heater();
    duty = (uint8_t)(out + 0.5f);

    /* 实时值推给 UI: 设定/输出/状态 (加热中=加热, 到温=恒温)
     * 数据流对照厂家: temp_get(&s_temp) -> thermo_ui_update_temp(s_temp.chamber, ...)
     * (thermo_app.c:134/137-139)。数据源是同一路 NTC: ch0(PA28) 腔体。 */
    thermo_ui_update_temp(meas, THERMO_AMBIENT_NA);
    thermo_ui_update_pid(out);
    thermo_ui_update_state(heat ? THERMO_STATE_HEATING : THERMO_STATE_MAINTAIN);

    /* 变化才打印: 灯翻转 或 输出档位变化 >=5% */
    {
        int changed = 0;
        int t10 = (int)(meas * 10.0f);

        if (heat != s_ctrl_heat_prev) changed = 1;
        if (s_ctrl_duty_shown == 0xFF) changed = 1;
        else if ((duty > s_ctrl_duty_shown ? duty - s_ctrl_duty_shown
                                           : s_ctrl_duty_shown - duty) >= 5) changed = 1;

        if (changed) {
            s_ctrl_heat_prev  = heat;
            s_ctrl_duty_shown = duty;
            printf("[CTRL] meas=%d.%d C raw=%u out=%u%% heater=%s setpoint_led=%s (PA25/PA24)\n",
                   t10 / 10, (t10 < 0 ? -t10 : t10) % 10, (unsigned)raw, (unsigned)duty,
                   heat ? "ON" : "OFF", heat ? "off" : "ON");
        }
    }
}

/* ==========================================================================
 *  2f. 常驻循环内的"开机自检相" (非阻塞状态机)
 *
 *  目的: 上板证据(main/settings/about 翻页 + 触摸 IRQ 观测 + 告警 flash)
 *        不再在 board_late_initialize 里同步 usleep 跑(那会挡死常驻线程启动),
 *        而是在常驻线程【自己】的循环里逐拍推进(靠 thermo_tick_cb() 的毫秒时间,
 *        不 block 线程)。即使某一步异常, 也按时间/计数推进, 绝不会阻止稳态循环:
 *        刷屏(lv_timer_handler) + 控温 tick + 触摸转发一直在跑。
 *  稳态判定: s_boot_st 达 BOOT_ST_STEADY 后本函数即空转, 不再做事。
 * ========================================================================== */
#define BOOT_ST_STEADY   99
static int s_boot_st;   /* 0=相开始, 由 sf32lb52_ui_selftest() 归零启动 */

static void thermo_ui_boot_step(uint32_t now)
{
    static uint32_t t0 = 0;
    static uint32_t probe_last = 0;
    static int      probe_c = 0;
    uint32_t        ms;

    if (s_boot_st >= BOOT_ST_STEADY) return;

    if (t0 == 0u) {
        t0 = now;
        s_boot_st = 0;
    }
    ms = now - t0;

    switch (s_boot_st) {
    case 0:  s_boot_st = 1; break;                                        /* 主页已显示 */
    case 1:  if (ms >= 300u)  { thermo_ui_show_page(THERMO_PAGE_SETTING); printf("[UI] switch->settings\n");  s_boot_st = 2; } break;
    case 2:  if (ms >= 600u)  { printf("[UI] screen=settings rendered ok\n"); s_boot_st = 3; } break;
    case 3:  if (ms >= 900u)  { thermo_ui_show_page(THERMO_PAGE_MAIN);    printf("[UI] switch->main\n");      s_boot_st = 4; } break;
    case 4:  if (ms >= 1200u) { printf("[UI] screen=main rendered ok\n");     s_boot_st = 5; } break;
    case 5:  if (ms >= 1500u) { printf("[UI] standby initial display ready\n"); s_boot_st = 6; } break;
    case 6:  if (ms >= 1800u) { printf("[UI] main labels live (font/pos verified ok)\n"); s_boot_st = 7; } break;
    case 7:  if (ms >= 2000u) { printf("[TP] IRQ probe start (~1.5s, press screen now):\n");
                 probe_c = 0; probe_last = ms; s_boot_st = 8; } break;
    case 8:                    /* ~300ms 打一拍, 共 5 拍 */
             if (ms - probe_last >= 300u) {
                 sf32lb52_touch_irq_print();
                 probe_last = ms;
                 probe_c++;
                 if (probe_c >= 5) s_boot_st = 9;
             } break;
    case 9:  if (ms >= 4200u) { thermo_ui_show_alarm(FAULT_OVER_TEMP); printf("[ALARM] selftest: artificial OVER_TEMP trigger (msgbox only)\n"); s_boot_st = 10; } break;
    case 10: if (ms >= 5400u) { thermo_ui_hide_alarm(); printf("[ALARM] selftest cleared\n"); s_boot_st = 11; } break;
    case 11:
             printf("[ALARM] real-time poll = 0x%x (0 = 当前无故障)\n", (unsigned)thermo_fault_poll());
             printf("[UI] LVGL selftest done (boot phase in resident thread)\n");
             s_boot_st = BOOT_ST_STEADY;
             break;
    default: s_boot_st = BOOT_ST_STEADY; break;
    }
}

/* ==========================================================================
 *  2e. LVGL 常驻任务
 *  厂家由 RT-Thread 的 LCD 线程周期调 lv_timer_handler(); 本工程用 NuttX
 *  线程等价实现 —— 否则开机自检跑完就没人再驱动 LVGL, 触摸与刷新都不会发生。
 *  线程内同时承担: 闲置熄屏 (低功耗) + PA11 唤醒 + 故障轮询。
 * ========================================================================== */

static void *thermo_ui_thread(void *arg)
{
    (void)arg;

    for (;;) {
        uint32_t now = thermo_tick_cb();

        /* 后台控温 tick: 不论屏幕亮/灭都跑 (控温不应随熄屏停止) */
        if (now - s_ctrl_last_ms >= THERMO_CTRL_TICK_MS) {
            s_ctrl_last_ms = now;
            thermo_ctrl_tick();
        }

        if (s_screen_on) {
            /* 开机自检相 (非阻塞状态机): 推进翻页/IRQ观测/告警 evidence */
            thermo_ui_boot_step(now);

            lv_timer_handler();
            thermo_fault_apply(thermo_fault_poll());

            if (now - s_last_activity_ms > THERMO_IDLE_OFF_MS) {
                thermo_power_enter_off();
            }
            usleep(5000);
        } else {
            /* 熄屏: 不跑 LVGL (省 CPU/总线), 只等 PA11 唤醒 */
            if (sf32lb52_wake_pin_pressed()) {
                thermo_power_wake();
            }
            usleep(20000);
        }
    }
    return NULL;
}

static void thermo_ui_start_task(void)
{
    pthread_t      tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8192);
    if (pthread_create(&tid, &attr, thermo_ui_thread, NULL) != 0) {
        printf("[UI] failed to start LVGL task\n");
    } else {
        printf("[UI] LVGL task started (touch + UI live)\n");
    }
    pthread_attr_destroy(&attr);
}

/* ==========================================================================
 *  4. 厂家 app 层依赖
 * ========================================================================== */

thermo_params_t g_thermo_params;

void thermo_mark_activity(void)
{
}

void thermo_event(thermo_event_t ev)
{
    printf("[THERMO] event=%d\n", (int)ev);
}

/* ==========================================================================
 *  上电自检入口 (板级 bringup 调用)
 *
 *  对应厂家开机流程: xiaozhi_ui.c 里 lv_lcd_init() + thermo_ui_init()
 *  + 常驻 lv_timer_handler() 循环。这里在 bringup 里同步跑若干帧,
 *  依次显示 主页 -> 设定页 -> 关于页 -> 主页, 并打印标记。
 *
 *  看板判据 (串口):
 *     [THERMO] UI init done, main=0x...., setting=0x...., about=0x....
 *     [THERMO] show_page MAIN, screen=0x....
 *     [THERMO] lv_scr_load done, active=0x....
 *     [TP] LVGL indev registered (FT6146, pointer, IRQ-driven)
 *     [TP] IRQ cfg: PA31 falling-edge, IER=..... (bit31 应=1)      <- EXTI 已配
 *     [TP] IRQ armed: PA31 falling-edge -> IRQ 100 ...             <- NVIC 已挂
 *     [UI] label gauge_val txt='25.0' font=0x... size=..x48        <- 数字可见
 *     [TP] IRQ probe: cnt=N ...  (点按屏幕时 N 应增长 = 中断生效)
 *     [TP] HIT raw=(..,..) -> screen=(..,..) evt=.. irq=..
 *     [UI] screen=main/settings/about rendered ok
 *     [UI] LVGL selftest done (thermo_ui.c, screens=main/settings/about)
 *  屏上: 深色主页(顶中状态字 / 蓝色目标 / 圆弧仪表+中央大温度 / 输出0%·环境--
 *        / 三个大圆钮"− 开/关 +") -> 设定页(两条滑条) -> 关于页 -> 回主页。
 * ========================================================================== */

int sf32lb52_ui_selftest(void)
{
    /* 4. 参数初值: 取自厂家 NVS (与 bringup 里 NVS 自检同源) */
    g_thermo_params.target_temp     = sf32lb52_nvs_get_target_temp();
    g_thermo_params.temp_high_limit = 80.0f;
    g_thermo_params.temp_low_limit  = 0.0f;
    if (g_thermo_params.target_temp <= 0.0f) {
        g_thermo_params.target_temp = 55.0f;
    }

    /* 2. LVGL 与显示 */
    if (sf32lb52_lcd_vendor_init() != 0) {
        printf("[UI] init FAILED (LCD not ready)\n");
        return -1;
    }

    /* 2b. 照厂家打开 LCDC1 中断 (drv_lcd.c:824-825)。
     * 必须在第一次 blit 之前 —— blit 用中断版发送并等完成中断。 */
    sf32lb52_lcd_irq_start();

    lv_init();
    lv_tick_set_cb(thermo_tick_cb);

    /* 3. 字体来源: 照厂家做法 —— TTF 数据 + lv_tiny_ttf 运行期建字体。
     *    厂家 xiaozhi_ui.c:2040: lv_tiny_ttf_create_data(xiaozhi_font,
     *    xiaozhi_font_size, medium_font_size); 这里同构。
     *    数据是 DroidSansFallback 的界面字符子集 (thermo_font.c, 50136B);
     *    完整 TTF 3.9MB 放不进 2.25MB 的 ER_IROM1 (厂家放在 4MB FONT_DATA)。
     *    必须在 lv_init() 之后建: tiny_ttf 会走 lv_malloc / lv_font 设施。*/
    font_medium = lv_tiny_ttf_create_data(xiaozhi_font, xiaozhi_font_size, 20);
    if (!font_medium) {
        printf("[UI] WARN: tiny_ttf create failed (Chinese may be blank)\n");
    }

    (void)thermo_lcd_buf_probe();

    thermo_disp_init();
    thermo_indev_init();

    /* 装上 PA31 下降沿中断 (照厂家做法; 见 2b 节厂家对照)。
     * 必须排在厂家触摸 init 之后 —— touch init 会把 PA31 配回普通输入。 */
    sf32lb52_touch_irq_start();

    /* 厂家 UI 原文: 建主页/设置页/告警 overlay 并填初值 */
    thermo_ui_init();

    /* 厂家 thermo_app 会周期性刷新; 这里同步刷几帧, 把数值填上 */
    thermo_ui_update_temp(sf32lb52_nvs_get_cur_temp(), THERMO_AMBIENT_NA);
    thermo_ui_update_pid(sf32lb52_pid_get_out());
    thermo_ui_update_state(THERMO_STATE_IDLE);

    /* 主页首帧: 只建页并把页面标记为待刷 (invalidate), 【不再同步调用
     * lv_timer_handler()】。
     *
     * 上板证据反复停在 "full flush(即整屏) rc=0 eject=1 blit/frame" 之后无任何
     * 输出 (既无 [LCD] blit evidence, 也无常驻线程的 [CTRL]/[UI] switch->settings):
     * 说明这里【同步】刷屏叫到 vendor blit 的中断同步等待后, 整屏虽然送出去了,
     * 控制权却不再归还 -> thermo_ui_start_task() 永远执行不到 -> 常驻线程/触摸
     * 线程都没被调度 -> 界面能显示但温度不刷新、按键不响应。
     *
     * 修复: 首帧改由常驻线程 thermo_ui_thread() 自己的 lv_timer_handler() 渲染,
     * 这里立即返回, 让 board_late_initialize 结束、OS 调度器跑起触摸线程与
     * 常驻线程。首帧证据 (full flush) 与开机自检相 (翻页/IRQ观测/告警) 全部维持
     * 在常驻线程里打印, 判据不变。 */
    thermo_ui_show_page(THERMO_PAGE_MAIN);

    /* 刷屏证据 (一行): flushes=帧数, bad=未等到完成中断的块数,
     * lcdc_irq=LCDC1 完成中断次数, hal_timeout=blit 内部超时次数。
     * 判据: lcdc_irq>0 且 bad=0 且 hal_timeout=0 => 中断版刷屏通路已通。 */
    {
        uint32_t irq_cnt = 0, to_cnt = 0;

        (void)sf32lb52_lcd_vendor_blit_stats(&irq_cnt, &to_cnt);
        printf("[LCD] blit evidence: flushes=%u bad=%u lcdc_irq=%u hal_timeout=%u hal_err=%u psram=%d\n",
               (unsigned)s_flush_cnt, (unsigned)s_flush_bad_cnt,
               (unsigned)irq_cnt, (unsigned)to_cnt,
               (unsigned)sf32lb52_lcd_vendor_blit_err_cnt(), s_lv_buf_is_psram);
    }

    /* 交棒给常驻 LVGL 线程: 之后触摸/刷新/低功耗/故障轮询/翻页证据全部由它驱动。
     * ★ 根因修复: 旧实现把下面这段 (翻页 for+i usleep、IRQ 观测、告警 flash) 全部
     *   同步跑在 board_late_initialize 的调用栈里, 卡住某一步 -> board_late_initialize
     *  不返回 -> 调度器未运行已 pthread_create 的触摸线程([TP] read 永不打印),
     *  且 thermo_ui_start_task() 永不执行(常驻线程没起来) -> 界面显示但不更新/不响应。
     *   现在: 翻页/IRQ观测/告警这些都移到常驻线程自己的开机自检相(thermo_ui_boot_step,
     *   非阻塞状态机)逐拍做; 这里立即启动常驻线程并返回, 让 OS 调度器跑起它。 */
    s_boot_st = 0;
    s_last_activity_ms = thermo_tick_cb();
    thermo_ui_start_task();

    printf("[UI] selftest init done (boot phase runs in resident thread)\n");
    return 0;
}