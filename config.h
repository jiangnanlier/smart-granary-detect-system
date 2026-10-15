/*
 * ============================================================================
 *  config.h —— 智能粮仓检测系统 · 统一配置与全局数据管理
 * ============================================================================
 *
 * 【文件职责】
 *   本文件集中管理系统的全部配置参数、数据类型定义与全局变量：
 *     - 一、系统参数配置：所有可调参数（阈值、周期、按键映射等）；
 *     - 二、数据类型定义：告警码、AI 识别结果、系统全局状态结构体；
 *     - 三、全局变量定义：main.c 使用的全部全局变量实体。
 *   修改系统参数或全局变量初值只需编辑本文件，无需翻阅 main.c。
 *
 * 【关于全局变量的定义方式】
 *   原 configure.c 中的变量定义已合并至本文件第三节。本项目仅 main.c
 *   包含本头文件，故以 static 链接属性在头文件中直接定义变量实体：
 *     - 每个包含本头文件的 .c 获得独立副本，绝不会产生重复定义的
 *       链接错误；
 *     - 仅 main.c 使用这些变量，运行行为与原来的单一定义完全一致。
 *   注意：若将来其他 .c 文件也需要共享这些变量，请改回经典模式
 *        （本文件用 extern 声明 + 单独一个 .c 文件定义）。
 *
 * 【使用方式】
 *   main.c 通过 #include "config.h" 引用全部配置与全局变量；
 *   编译命令：
 *     gcc -Wall -O2 main.c driver/*.c -o smart_granary -lpthread -lm
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

#include "driver/key.h"      /* key_device_t、KEY_1/KEY_2/KEY_3 按键码 */
#include "driver/my_timer.h" /* Timer_Handle 定时器句柄类型           */
#include "driver/fan.h"      /* FAN_PWM_MAX 风扇占空比上限            */

/* ============================================================================
 * 一、系统参数配置（所有可调参数集中于此，按需修改）
 * ========================================================================== */

/* ---- 1.1 可调档位电压传感器（模拟输出 0~3V）----
 * 传感器输出 0~3V 对应真实供电 0~6V：真实电压 = ADC 采样电压 × 2
 *   档位1 标准供电 2.50V → 5.00V（安全区间 4.8~5.2V）
 *   档位2 欠压工况 2.30V → 4.60V（低于下限，触发 E01）
 *   档位3 过压工况 2.70V → 5.40V（高于上限，触发 E01）
 * 档位由硬件拨码切换，软件只负责 ADC 采样与换算。 */
#define VOLTAGE_CONVERT_RATIO   2.0f
#define VOLTAGE_SAFE_MIN        4.8f    /* 供电安全下限 (V) */
#define VOLTAGE_SAFE_MAX        5.2f    /* 供电安全上限 (V) */

/* ---- 1.2 模拟负载电流规则 (A) ----
 * 整机电流按当前开启的外设累加，用于实时功率计算 P = U × I */
#define CURRENT_BASE_STANDBY    0.10f   /* 基础待机电流 */
#define CURRENT_FAN             0.15f   /* 风扇开启时附加 */
#define CURRENT_BUZZER          0.08f   /* 蜂鸣器鸣叫时附加 */
#define CURRENT_LED_ALL         0.05f   /* LED 全部点亮 / 闪烁时附加 */

/* ---- 1.3 储粮安全阈值 ---- */
#define TEMPERATURE_MAX         30.0f   /* 温度上限 (℃)   */
#define HUMIDITY_MAX            70.0f   /* 湿度上限 (%RH) */

/* ---- 1.4 任务周期 (ms) ---- */
#define SENSOR_POLL_PERIOD_MS    1000   /* 传感器轮询周期 */
#define CAMERA_CAPTURE_PERIOD_MS 5000   /* 摄像头采集 / AI 识别周期 */
#define MAIN_LOOP_PERIOD_MS      100    /* 主循环周期（告警输出刷新粒度） */
#define LCD_REFRESH_PERIOD_MS    1000   /* LCD / 网页数据刷新周期 */

/* ---- 1.5 蜂鸣器告警音参数 (Hz) ---- */
#define BUZZER_LOW_FREQ         1000U   /* E01 低频间断鸣响频率 */
#define BUZZER_HIGH_FREQ        4000U   /* E03~E05 高频持续报警频率 */
#define BUZZER_E01_ON_MS        200     /* E01 每次鸣响时长 */
#define BUZZER_E01_OFF_MS       600     /* E01 每次静音间隔 */

/* ---- 1.6 LED 交替闪烁相位 (ms) ---- */
#define LED_BLINK_PHASE_MS      200     /* 每 200ms 切换一次点亮的 LED */

/* ---- 1.7 风扇占空比 ---- */
#define FAN_ON_PWM              FAN_PWM_MAX  /* 通风时全速运转 */
#define FAN_OFF_PWM             0U

/* ---- 1.8 按键映射 ----
 * 独立按键在 Linux 输入子系统中的键值；
 * 如实际硬件映射不同，仅需修改此处即可。 */
#define KEY1_CODE               KEY_1   /* KEY1：巡检启动 / 暂停 */
#define KEY2_CODE               KEY_2   /* KEY2：手动抓拍 + AI 即时分析 */
#define KEY3_CODE               KEY_3   /* KEY3：蜂鸣器静音开关 */

/* ---- 1.9 AI 图像 Base64 缓冲区大小 ----
 * 按实际摄像头单帧 JPG 大小调整（Base64 编码后约为原图 4/3） */
#define AI_IMAGE_BUFFER_SIZE    (256 * 1024)

/* ============================================================================
 * 二、数据类型定义
 * ========================================================================== */

/* 告警码（对应功能说明文档 4.3 节分级告警） */
typedef enum {
    ALARM_NONE = 0,        /* 无告警                                    */
    ALARM_E01_VOLTAGE,     /* E01 供电电压异常（欠压 / 过压）           */
    ALARM_E02_TEMP_HUMID,  /* E02 温湿度超出储粮安全阈值                */
    ALARM_E03_PERSON,      /* E03 人员非法闯入                          */
    ALARM_E04_GRAIN,       /* E04 粮情异常（霉变 / 塌陷 / 虫害 / 杂物） */
    ALARM_E05_FIRE,        /* E05 明火隐患                              */
} alarm_code_t;

/* 多模态大模型识别结果 */
typedef struct {
    bool person_intrusion;  /* 人员非法闯入        */
    bool grain_mold;        /* 粮食霉变结块        */
    bool grain_collapse;    /* 粮面塌陷            */
    bool pest_infestation;  /* 虫害堆积            */
    bool debris_left;       /* 杂物遗留            */
    bool fire_hazard;       /* 明火隐患            */
    char description[256];  /* 大模型画面描述文本（LCD 显示用） */
} ai_result_t;

/* 系统全局状态（访问需持有 g_state_mutex） */
typedef struct {
    /* 巡检任务控制 */
    bool inspection_running;  /* true=巡检运行中 false=已暂停（KEY1 控制） */
    bool buzzer_muted;        /* 蜂鸣器静音标志（KEY3 控制）               */

    /* 传感器实时读数 */
    float voltage;            /* 系统供电电压 (V)，已按 ×2 换算 */
    bool  voltage_valid;      /* 电压数据有效标志   */
    float temperature;        /* 温度 (℃)           */
    float humidity;           /* 湿度 (%RH)         */
    bool  ht_valid;           /* 温湿度数据有效标志 */
    float light;              /* 光照强度 (lux)     */
    bool  light_valid;        /* 光照数据有效标志   */
    bool  person_detected;    /* 人体红外：有人闯入 */
    bool  fire_detected;      /* 火焰传感器：检测到明火 */

    /* 功率参数（P = U × I） */
    float current;            /* 模拟负载电流 (A) */
    float power;              /* 实时功率 (W)     */

    /* 执行机构状态（用于电流累加与避免重复写 sysfs） */
    bool fan_on;              /* 风扇开启          */
    bool buzzer_sounding;     /* 蜂鸣器正在鸣叫    */
    bool led_all_on;          /* LED 全部点亮/闪烁中 */

    /* 告警状态 */
    alarm_code_t alarm;       /* 当前告警码   */
    alarm_code_t last_alarm;  /* 上一次告警码（边沿检测：抓拍 / 网页刷新） */

    /* AI 识别结果 */
    ai_result_t ai;
    bool ai_valid;
} system_state_t;

/* ============================================================================
 * 三、全局变量定义
 * ----------------------------------------------------------------------------
 * 原 configure.c 的变量定义已合并至本节。本项目仅 main.c 包含本头文件，
 * 故以 static 链接属性直接定义变量实体：每个包含本头文件的 .c 获得独立
 * 副本，不会产生重复定义的链接错误；仅 main.c 使用这些变量，运行行为
 * 与原来的单一定义完全一致。
 * 若将来其他 .c 文件也需要共享这些变量，请改回经典模式：
 *   本节改用 extern 声明，并在唯一一个 .c 文件中定义。
 * ========================================================================== */

/* 系统全局状态（零初始化：巡检暂停、无告警、各数据标记无效） */
static system_state_t g_state;

/* 按键设备句柄（key_init_auto 成功后填充） */
static key_device_t g_key;

/* 传感器轮询定时器句柄，-1 表示尚未创建 */
static Timer_Handle g_sensor_timer = -1;

/* 摄像头采集 / AI 识别定时器句柄，-1 表示尚未创建 */
static Timer_Handle g_camera_timer = -1;

/* 保护全局状态的互斥锁（静态初始化，无需调用 pthread_mutex_init） */
static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 主循环运行标志：收到退出信号（SIGINT/SIGTERM）时置 0 */
static volatile sig_atomic_t g_running = 1;

/* 传感器定时节拍标志：定时器回调置 1，主循环消费后清零 */
static volatile sig_atomic_t g_sensor_tick = 0;

/* 摄像头定时节拍标志：定时器回调置 1，主循环消费后清零 */
static volatile sig_atomic_t g_camera_tick = 0;

#endif /* CONFIG_H */
