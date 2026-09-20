/****************************************************************************
 * boards/arm/sf32lb52/sf32lb52-lcd/src/sf32lb52_lcd_bringup.c
 *
 * SF32LB52-LCD 板级初始化  —— M1 完成版 (已去掉全部调试探针)
 *
 * NuttX 钩子:
 *   CONFIG_BOARD_LATE_INITIALIZE=y → nx_start() 在 OS 初始化完成后调用
 *                                    board_late_initialize();
 *   CONFIG_BOARDCTL=y              → boardctl(BOARDIOC_INIT) 会调用
 *                                    board_app_initialize()。
 *
 * 调试期间在这里埋过 [M1] P1a~P1d / P2a~P2b 标记; M1 已于
 * 2026-09-15 上板验证通过 (NuttShell + nsh> 提示符出现), 标记全部移除。
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <syslog.h>

/* 芯片层提供的串口注册函数 (见 arch/arm/src/sf32lb52/sf32lb52_serial.c)。
 * 内部幂等: 注册成功才置标志, 重复调用安全。
 */

void arm_serialinit(void);

/* 芯片层提供的 RX 字节级诊断启动入口 (M3, 见 arch/arm/src/sf32lb52/
 * sf32lb52_serial.c)。内部幂等: 启动一个低优先级内核线程, 每秒打印一次
 * [RXDIAG] 计数行。只做诊断, 不改动收发逻辑。
 */

void sf32lb52_rx_diag_start(void);

/* ---------------------------------------------------------------------------
 * 厂家 LCD 栈 API (来自 libsf32lb52_vendorlcd.a, 见 build_vendor_lcd_lib.sh)
 *
 *   库内是厂家 HAL + 厂家 co5300 屏驱动原文, 已在裸机探针上上板验证:
 *     CO5300_ReadID 0x00331100 (= LCD_ID) + 三条色带可见。
 * ------------------------------------------------------------------------ */

int      sf32lb52_lcd_vendor_init(void);
void     sf32lb52_lcd_vendor_clear(void);
void     sf32lb52_lcd_vendor_fill(uint16_t x0, uint16_t y0, uint16_t x1,
                                  uint16_t y1, uint8_t r, uint8_t g, uint8_t b);
uint32_t sf32lb52_lcd_vendor_readpixel(uint16_t x, uint16_t y);
void     sf32lb52_lcd_vendor_setbrightness(uint32_t percent);
uint32_t sf32lb52_lcd_vendor_readid(void);

/* Task A/B 自检入口 (在 libsf32lb52_vendorlcd.a 的 LCD 胶水层里):
 *   solidtest()           整屏 红/绿/蓝 依次 1s, 走已验证的 lcd_fill_raw 通路
 *   readpixel_selftest()  整屏纯色 -> 多点回读自证, 决定 readpixel 是否可信
 *   readpixel_usable()    上面的结论 (1=可信, 0=不可用, UI 回读校验据此降级) */
void     sf32lb52_lcd_vendor_solidtest(void);
int      sf32lb52_lcd_vendor_readpixel_selftest(void);
int      sf32lb52_lcd_vendor_readpixel_usable(void);

/* ---------------------------------------------------------------------------
 * 厂家触摸栈 API (与 LCD 在同一个 .a 里, 见 build_vendor_lcd_lib.sh)
 *   FT6146, I2C1 (PA30=SCL / PA33=SDA), TP_RESET=PA09, CTP_INT=PA31
 *   寄存器布局 = FT5x06 标准 (0x38, TD_STATUS=0x02, P1_XH=0x03, ID=0xA3/0x9F)
 * ------------------------------------------------------------------------ */

int      sf32lb52_touch_vendor_init(void);
uint32_t sf32lb52_touch_vendor_readid(void);
int      sf32lb52_touch_vendor_read(uint16_t *x, uint16_t *y, uint8_t *evt);
void     sf32lb52_touch_vendor_probe(void);
int      sf32lb52_touch_vendor_selftest(void);

/* ---------------------------------------------------------------------------
 * 厂家 NTC 采温栈 API (同样在 libsf32lb52_vendorlcd.a 里, 见 build_vendor_lcd_lib.sh)
 *   SiFli 片内 GPADC (hwp_gpadc1): ch0 = PA28 腔体 NTC / ch1 = PA29 环境 NTC
 *   初始化参数 / 读取时序 / R-T 换算 全部照厂家 app/src/temp/temp.c
 *     ch<0 或 >=2 返回 -1; 开路时 temp_c = -999.0f
 * ------------------------------------------------------------------------ */

int      sf32lb52_ntc_init(void);
int      sf32lb52_ntc_read(int ch, uint16_t *raw, float *temp_c);

/* ---------------------------------------------------------------------------
 * 厂家 PWM 风机栈 API (同样在 libsf32lb52_vendorlcd.a 里, 见 build_vendor_lcd_lib.sh)
 *   PA32 = GPTIM2_CH1 (厂家 pwm3), 载波 1kHz, 占空比 0..100%
 *   厂家 app/src/ctrl/ctrl.c 用 pwm2/GPTIM1_CH1 驱动风机; 本板 GPTIM1_CH4 已被
 *   背光占用, 改周期会影响背光, 故风机改用 GPTIM2_CH1 (PA32),
 *   引脚复用依据 sdk/.../bsp_pinmux.c: HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, ...)
 * ------------------------------------------------------------------------ */

int      sf32lb52_fan_init(void);
int      sf32lb52_fan_set_duty(uint8_t pct);
int      sf32lb52_fan_selftest(void);

/* ---------------------------------------------------------------------------
 * 厂家参数存储 (NVS) API (同样在 libsf32lb52_vendorlcd.a 里)
 *   做法照厂家 app/src/storage/storage.c: 参数结构体 + CRC32 校验;
 *   落盘走厂家 QSPI NOR 编程栈 (bf0_hal_mpi_ex.c HAL_QSPIEX_FLASH_WRITE/ERASE),
 *   即 sftool 在 SRAM 里烧 flash 的同一套原语, 存在厂家 KVDB 区
 *   0x12458000 (ptab.h, 一个 4KB 扇区), 不碰运行代码所在的 XIP 区。
 *   boot_count 每次上电 +1, 用于验证掉电保持。
 * ------------------------------------------------------------------------ */

int      sf32lb52_nvs_init(void);
int      sf32lb52_nvs_load(void);
int      sf32lb52_nvs_save(void);
int      sf32lb52_nvs_selftest(void);
int      sf32lb52_nvs_set(float target_temp, uint8_t fan_duty);
float    sf32lb52_nvs_get_target_temp(void);
uint8_t  sf32lb52_nvs_get_fan_duty(void);
uint32_t sf32lb52_nvs_get_boot_count(void);
float    sf32lb52_nvs_get_cur_temp(void);
int      sf32lb52_nvs_set_cur_temp(float cur_temp);

/* ---------------------------------------------------------------------------
 * PID 控温栈 API (同样在 libsf32lb52_vendorlcd.a 里)
 *   算法/参数逐行照厂家 app/src/ctrl/ctrl.c:
 *     增量式 PID (ctrl.c:51-72), kp=6.0/ki=0.2/kd=1.0 (ctrl.c:170-175),
 *     输出限幅 0..100% (ctrl.c:64-65), 加热滞回 2.0/0.0 (ctrl.c:22-24),
 *     风机最低 30% 防闷烧 (ctrl.c:26)
 *   本板落点: 腔体 NTC(ch0/PA28) -> PID -> 风机 PWM(PA32/GPTIM2_CH1)
 * ------------------------------------------------------------------------ */

void  sf32lb52_pid_init(void);
void  sf32lb52_pid_reset(void);
float sf32lb52_pid_step(float target, float measured);
float sf32lb52_pid_get_out(void);
int   sf32lb52_pid_selftest(void);

/* ---------------------------------------------------------------------------
 * GPIO 胶水层 API (同样在 libsf32lb52_vendorlcd.a 里)
 *   加热指示灯 PA25 (LED_A, 输出, 高=亮) / 到温指示灯 PA24 (LED_B, 输出, 高=亮)
 *   / 唤醒按键 PA11 (输入)
 *   引脚/电平约定照厂家 app/src/ctrl/ctrl.c 的 rt_pin_write(GPIO_RELAY_HEAT):
 *   纯 GPIO, 高电平有效。到温灯(PA24)= !heating 是本工程新增的映射。
 *   (原定 PA21=加热灯 / PA26=到温灯; 该两脚在开发板排针上未引出, 本次改到 PA25/PA24)
 * ------------------------------------------------------------------------ */

int sf32lb52_heater_init(void);
int sf32lb52_heater_set(int on);
int sf32lb52_setpoint_led_init(void);
int sf32lb52_setpoint_led_set(int on);
int sf32lb52_heat_pair_apply(int heater_on);
int sf32lb52_heat_pair_selftest(void);
int sf32lb52_wake_pin_init(void);
int sf32lb52_wake_pin_pressed(void);
int sf32lb52_gpio_selftest(void);

/* ---------------------------------------------------------------------------
 * UI 栈 API (本次改为"照厂家移植")
 *   UI 现为厂家 app/src/thermo_ui.c 原文 (LVGL): 主页 MAIN + 设置页 SETTING
 *   两个 lv_scr (thermo_ui.h:15-16), 告警是 lv_layer_top() 上的 overlay。
 *   源码在 apps/thermo_panel (libapps.a), 由 thermo_ui_glue.c 提供
 *   LVGL 显示落屏 (flush_cb -> sf32lb52_lcd_vendor_blit) 与字体/参数依赖。
 *   因此下面的 sf32lb52_ui_init/show/sync/set_values 声明仅作历史保留
 *   (新实现不导出它们), 真正被调用的只有 sf32lb52_ui_selftest()。
 * ------------------------------------------------------------------------ */

int  sf32lb52_ui_init(void);
void sf32lb52_ui_show(int screen);
void sf32lb52_ui_sync(void);
void sf32lb52_ui_set_values(float target, float meas, float amb,
                            float duty, float limit);
int  sf32lb52_ui_selftest(void);
int  sf32lb52_lcd_vendor_blit(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              const void *pix);

/****************************************************************************
 * 板级 bringup (内部)
 ****************************************************************************/

static int sf32lb52_lcd_bringup(void)
{
  syslog(LOG_INFO, "[BOARD] SF32LB52-LCD bringup BUILD-TAG-0920M2FIX (M2: console + LCD)\n");

#ifdef CONFIG_SF32LB52_HAS_LCDC
  /* M2 阶段: 点亮屏并自证显示通路 (画三条色带 + 从 GRAM 读回) */
  sf32lb52_lcd_vendor_init();
  syslog(LOG_INFO, "[LCD] ReadID = 0x%08x (期望 0x00331100)\n",
         (unsigned int)sf32lb52_lcd_vendor_readid());

  sf32lb52_lcd_vendor_fill(0,   0, 389, 149, 255, 0,   0);
  sf32lb52_lcd_vendor_fill(0, 150, 389, 299, 0,   255, 0);
  sf32lb52_lcd_vendor_fill(0, 300, 389, 449, 0,   0,   255);
  syslog(LOG_INFO, "[LCD] bars: R=0x%x G=0x%x B=0x%x (期望 F800/7E0/1F)\n",
         (unsigned int)sf32lb52_lcd_vendor_readpixel(195,  75),
         (unsigned int)sf32lb52_lcd_vendor_readpixel(195, 225),
         (unsigned int)sf32lb52_lcd_vendor_readpixel(195, 375));

  sf32lb52_lcd_vendor_setbrightness(80);
  syslog(LOG_INFO, "[LCD] backlight = 80%%\n");

  /* ---- Task B: 整屏纯色 红/绿/蓝 自检 (各 1s, 走已验证的 lcd_fill_raw 通路) ----
   *   看板判据(肉眼 + 串口):
   *     [UI] solid screen=red / green / blue 三行按序出现, 屏上应依次是
   *     纯红 -> 纯绿 -> 纯蓝 三整屏。
   *     - 顺序与颜色都对: 面板/时序/RGB565 格式全 OK, 三屏不出来的原因
   *       就只可能在 UI 绘制侧;
   *     - 颜色错位(红显示成蓝等): RGB565 字节序/位序问题;
   *     - 三屏同色: 写窗口或数据源问题。
   *   注意: 本步会覆盖上面那三条色带 (正常)。 */
  sf32lb52_lcd_vendor_solidtest();

  /* ---- Task A: readpixel 自证 (整屏纯色 -> 多点回读) ----
   *   只有这里全 OK, 后面的 [UI] verify 才允许把回读值当判据;
   *   否则自动降级为 [UI] verify SKIPPED (readpixel unreliable)。 */
  (void)sf32lb52_lcd_vendor_readpixel_selftest();

  /* ---- 触摸: 与 LCD 共用同一个厂家 .a, 故暂用同一个 Kconfig 开关守护 ----
   *   (后续接入 NuttX input/ft5x06 时再拆出独立的 SF32LB52_HAS_TOUCH)
   *   这里只做"通路自证": 能读出非 0 / 非 0xFF 的芯片 ID, 就说明
   *   I2C1 时钟、引脚复用、TP 复位三件事全对。 */
  if (sf32lb52_touch_vendor_init() == 0)
  {
    syslog(LOG_INFO, "[TP] ID = 0x%02x (0xFF = I2C 无应答, 其它 = 通路正常)\n",
           (unsigned int)sf32lb52_touch_vendor_readid());
    sf32lb52_touch_vendor_probe();

    /* ---- 读点自检 (改动2): 无触摸也把原始寄存器读出来打印, 用来区分
     *   "读点逻辑没跑" 与 "在等触摸"; 并读芯片标识/固件/模式寄存器 +
     *   CTP_INT(PA31) 电平 + 1.5s 有界轮询。
     *   看板要点: [TP] readpoint selftest: regs0x01 ... TD_STATUS(n)=0 (无触摸);
     *             按住屏幕时出现 [TP] readpoint selftest HIT: ...;
     *             末行 i2c_ok=30/30 表示读通路活着。 */
    (void)sf32lb52_touch_vendor_selftest();
  }
  else
  {
    syslog(LOG_ERR, "[TP] init FAILED\n");
  }

  /* ---- NTC 采温: 与 LCD/触摸共用同一个厂家 .a, 故同样用这个 Kconfig 开关守护 ----
   *   只做"通路自证": 打印 ch0 的 12bit 原始值与换算温度。
   *     raw 正常范围约 0..4013; raw 接近 4013(>4013) = NTC 开路 -> 温度打 -999.0
   *   温度只用整数打印 (NuttX syslog 不带 %f): t/10 与 |t|%10 拼成一位小数
   *   ★ 本工程改动: 本板只接 1 路 NTC 到 PA28(ch0), PA29(ch1) 悬空。
   *     故这里【只读 ch0】; ch1 的断线判据已在 sf32lb52_ntc_read 内关闭
   *     (见 build_vendor_lcd_lib.sh 的 vendor_ntc_glue.c), 避免悬空脚误报断线。 */
  if (sf32lb52_ntc_init() == 0)
  {
    uint16_t raw    = 0;
    float    temp_c = -999.0f;
    int      rc     = sf32lb52_ntc_read(0, &raw, &temp_c);
    int      t10    = (int)(temp_c * 10.0f);      /* 温度 ×10 */

    syslog(LOG_INFO, "[NTC] ch0 (PA28) raw=%u temp=%d.%d C rc=%d\n",
           (unsigned int)raw,
           t10 / 10, (t10 < 0 ? -t10 : t10) % 10, rc);
  }
  else
  {
    syslog(LOG_ERR, "[NTC] init FAILED\n");
  }

  /* ---- PWM 风机上电自检: 依次 0% / 50% / 100%, 各保持 300ms, 最后回到 0% ----
   *   看板要点: 串口出 [FAN] init ok ... / [FAN] set duty=..% / [FAN] selftest done;
   *   若有示波器可量 PA32, 1kHz 方波, 占空比随打印变化。 */
  if (sf32lb52_fan_selftest() != 0)
  {
    syslog(LOG_ERR, "[FAN] selftest FAILED\n");
  }

  /* ---- 参数存储上电自检: 读 -> boot_count+1 -> 回写 -> 再读回校验 ----
   *   看板要点: 每次上电 [NVS] boot_count 应比上一次大 1
   *   (这是掉电保持成功的证据); 首次运行会用默认值 (target_temp=55.0, duty=30)。 */
  if (sf32lb52_nvs_selftest() != 0)
  {
    syslog(LOG_ERR, "[NVS] selftest FAILED\n");
  }

  /* ---- PID 控温自检 (排在 [NVS] 之后, 因为设定值/落盘都依赖 NVS 已加载) ----
   *   两段: [PID] real 真实闭环 (腔体 NTC -> PID -> 风机, 逐轮打印
   *         设定值/实测值/偏差/P-I-D 三项/输出占空比);
   *         [PID] sim  闭环仿真, 看增量式 PID 从 8°C 偏差收敛到 0。
   *   看板要点: [PID] real 每轮 out 非零且在动; [PID] sim 的 meas 逐轮逼近 target。 */
  if (sf32lb52_pid_selftest() != 0)
  {
    syslog(LOG_ERR, "[PID] selftest FAILED\n");
  }

  /* ---- GPIO / 双灯自检 (加热灯 PA25 = LED_A, 到温灯 PA24 = LED_B, 唤醒键 PA11) ----
   *   看板要点: 串口 [HEAT] setpoint LED init pin=PA24 / [HEAT] heater init pin=PA25 /
   *             [PWR] wake key init pin=PA11;
   *             随后 [HEAT] pair selftest 两态回读应为:
   *               加热中 -> PA25=1 PA24=0 (OK)
   *               到温   -> PA25=0 PA24=1 (OK)
   *             且最后停在到温态 (PA25=0 / PA24=1)。
   *   注: PID 自检里的加热滞回也会驱动这一对灯 (见 [HEAT] pair 打印)。 */
  if (sf32lb52_gpio_selftest() != 0)
  {
    syslog(LOG_ERR, "[GPIO] selftest FAILED\n");
  }

  /* ---- 三屏 UI 自检 (排在 [NVS]/[PID] 之后, 要用到它们的最新数值) ----
   *   依次渲染并定时切换 main -> settings -> about -> main, 每步打印标记。
   *   看板要点: 串口出现 [UI] screen=main/settings/about rendered ok
   *             与 [UI] switch->settings/about/main; 屏上三屏依次可见。 */
  if (sf32lb52_ui_selftest() != 0)
  {
    syslog(LOG_ERR, "[UI] selftest FAILED\n");
  }
#endif

  return OK;
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   由 nx_start() 在 OS 初始化完成后调用。此处补注册控制台设备:
 *   公共层通常已在 up_initialize() 阶段调用过 arm_serialinit(), 但那时
 *   文件系统可能尚未就绪、register_driver 会失败; 再调一次即可补注册
 *   (arm_serialinit 内部幂等), 确保 NSH 能拿到 /dev/console。
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  arm_serialinit();

  /* M3 诊断: 控制台就绪后启动 RX 计数打印线程 (只统计, 不改收发)。 */

  sf32lb52_rx_diag_start();

  (void)sf32lb52_lcd_bringup();
}
#endif

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   由 boardctl() 处理 BOARDIOC_INIT 时调用。
 *   注意: boards/Makefile 在 CONFIG_BOARDCTL=y 时会编入 boardctl.c,
 *         而 boardctl.c 引用本函数, 缺失即链接期报
 *         undefined reference to `board_app_initialize'。
 *
 *   M1 无板级应用初始化内容; M2 将在此初始化 LCD 面板、触摸、NTC ADC
 *   与 PWM 风机等。
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL
int board_app_initialize(uintptr_t arg)
{
  (void)arg;

  return sf32lb52_lcd_bringup();
}
#endif