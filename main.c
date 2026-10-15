/*
 * ============================================================================
 *  智能粮仓检测系统 · 主程序（main.c）
 *  平台：FS-MP1A 嵌入式 Linux 开发板 + 多模态大模型
 * ============================================================================
 *
 * 【系统概述】
 *   开发板周期性采集粮仓现场图像与传感器数据（可调档位电压、温湿度、光照、
 *   人体红外、火焰），图像经 Base64 编码后送多模态大模型识别；同时通过
 *   局域网向浏览器推流，实时查看监控画面。系统支持分级硬件告警、按键
 *   交互、异常图片本地保存等功能。
 *
 * 【硬件资源】
 *    1  FS-MP1A 主控开发板   系统主控，外设控制与数据处理
 *    2  摄像头模块           粮仓现场图像采集
 *    3  可调档位电压传感器   模拟输出 0~3V，拨码切换档位，ADC 采样换算供电电压
 *    4  温湿度传感器         采集仓内温度、湿度
 *    5  光照传感器           采集仓内光照强度
 *    6  人体红外传感器       检测仓内是否有人闯入（待接 GPIO 驱动）
 *    7  火焰传感器           检测仓内明火隐患（待接 GPIO 驱动）
 *    8  LED1/LED2/LED3       分级告警灯光指示
 *    9  直流风扇             温湿度超标时通风降湿
 *   10  蜂鸣器               声音告警
 *   11  KEY1/KEY2/KEY3       巡检启停 / 手动抓拍 / 蜂鸣器静音
 *
 * 【系统逻辑链】（对应功能说明文档第五部分）
 *   上电初始化 → 接入局域网开启网页推流 → 等待 KEY1 启动巡检 → 主循环：
 *     ① 传感器轮询：电压(ADC 换算) / 温湿度 / 光照 / 人体红外 / 火焰
 *     ② 功率计算：P = U × I（I = 基础待机电流 + 各开启外设的模拟负载电流）
 *     ③ 摄像头周期采集图像，Base64 编码后送多模态大模型识别
 *     ④ LCD 刷新传感器数据与 AI 识别结果；局域网网页同步叠加显示
 *     ⑤ 告警判定与分级硬件告警执行（E01~E05），异常自动抓拍存 JPG
 *     ⑥ 按键扫描：KEY1 巡检启停 / KEY2 手动抓拍 / KEY3 蜂鸣器静音
 *
 * 【告警分级】（对应功能说明文档 4.3 节）
 *   E01 电压异常   欠压 <4.8V 或 过压 >5.2V
 *                  → LED1 亮 + 蜂鸣器低频间断鸣响 + 自动抓拍 + 网页提示
 *   E02 温湿度超限 温度/湿度超出储粮安全阈值
 *                  → LED2 亮 + 风扇自动通风 + 网页提示
 *   E03 人员闯入   人体红外传感器 / AI 识别到人员
 *                  → LED1~3 交替闪烁 + 蜂鸣器高频持续 + 自动抓拍 + 网页提示
 *   E04 粮情异常   AI 识别到霉变结块 / 粮面塌陷 / 虫害 / 杂物遗留
 *                  → LED1~3 交替闪烁 + 蜂鸣器高频持续 + 自动抓拍 + 网页提示
 *   E05 明火隐患   火焰传感器 / AI 识别到明火
 *                  → LED1~3 交替闪烁 + 蜂鸣器高频持续 + 自动抓拍 + 网页提示
 *
 * 【多任务组织】
 *   两个 POSIX 定时器（driver/my_timer）产生任务节拍：
 *     - 传感器轮询定时器：1s 周期
 *     - 摄像头采集 / AI 识别定时器：5s 周期
 *   定时器回调只负责置位“节拍”标志；实际的采集、识别、告警、刷新等工作
 *   全部放在主循环中顺序执行 —— 单线程消费多节拍，避免共享状态并发冲突，
 *   逻辑直观、便于调试与移植。
 *
 * 【文件组织】
 *   config.h   —— 统一管理全部配置参数、数据类型与全局变量
 *                （修改阈值、周期、按键映射、变量初值请到此文件）
 *   main.c     —— 系统业务逻辑（本文件）
 *   driver/    —— 各外设驱动
 *
 * 【待对接模块（桩函数，见下文“外部功能模块接口”一节）】
 *   摄像头采集、多模态大模型 HTTP 调用、LCD 显示、局域网推流、
 *   人体红外 / 火焰传感器 GPIO 读取 —— 均以桩函数形式给出统一接口并标注
 *   TODO，对应驱动就绪后填充实现即可，主逻辑无需改动。
 *
 * 【编译方法】
 *   gcc -Wall -O2 main.c driver/*.c -o smart_granary -lpthread -lm
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "config.h"              /* 配置参数 / 数据类型 / 全局变量 */
#include "driver/delay.h"
#include "driver/key.h"
#include "driver/LED.h"
#include "driver/buzzer.h"
#include "driver/fan.h"
#include "driver/ht_sensor.h"
#include "driver/light_sensor.h"
#include "driver/voltage_sensor.h"
#include "driver/my_timer.h"

/* ============================================================================
 * 一、告警描述工具
 * ========================================================================== */

/* 告警码 → 中文描述（屏幕 / 网页 / 日志使用） */
static const char *alarm_name(alarm_code_t code)
{
    switch (code) {
    case ALARM_E01_VOLTAGE:    return "E01 供电电压异常";
    case ALARM_E02_TEMP_HUMID: return "E02 温湿度超出储粮安全阈值";
    case ALARM_E03_PERSON:     return "E03 人员非法闯入";
    case ALARM_E04_GRAIN:      return "E04 粮情异常(霉变/塌陷/虫害/杂物)";
    case ALARM_E05_FIRE:       return "E05 明火隐患";
    case ALARM_NONE:
    default:                   return "无告警";
    }
}

/* ============================================================================
 * 二、外部功能模块接口（桩函数）
 * ----------------------------------------------------------------------------
 * 摄像头、多模态大模型、LCD、局域网推流、人体红外 / 火焰传感器等模块的
 * 驱动尚未纳入 driver/ 目录，此处以桩函数定义统一接口并标注 TODO，
 * 待对应驱动 / 网络能力就绪后填充实现，主逻辑无需改动。
 * ========================================================================== */

/**
 * @brief 摄像头采集一帧画面并做 Base64 编码
 * @param buf   输出缓冲区（Base64 字符串，以 '\0' 结尾）
 * @param size  缓冲区大小
 * @return 成功返回编码后长度，失败返回 -1
 */
static int camera_capture_base64(char *buf, size_t size)
{
    /* TODO: 调用摄像头驱动（V4L2 / UVC）采集一帧 JPG 图像，
     *       再做 Base64 编码写入 buf。 */
    (void)buf;
    (void)size;
    return -1;
}

/**
 * @brief 多模态大模型图像识别
 *        将 Base64 编码图像与场景提示词送入大模型，识别：
 *        人员非法闯入 / 粮食霉变结块 / 粮面塌陷 / 虫害堆积 /
 *        杂物遗留 / 明火隐患
 * @param image_base64 Base64 图像数据
 * @param result      输出识别结果
 * @return 0 成功，-1 失败
 */
static int ai_analyze_image(const char *image_base64, ai_result_t *result)
{
    /* TODO: 组织多模态大模型 API 请求（图像 Base64 + 场景提示词），
     *       解析返回 JSON，填充以下字段：
     *       person_intrusion / grain_mold / grain_collapse /
     *       pest_infestation / debris_left / fire_hazard / description
     *       可选用 libcurl 发送 HTTPS 请求、json-c 解析响应。 */
    (void)image_base64;

    if (result == NULL) {
        return -1;
    }
    memset(result, 0, sizeof(*result));
    snprintf(result->description, sizeof(result->description),
             "AI 模块未接入（桩函数）");
    return -1;
}

/**
 * @brief LCD 屏幕刷新：显示大模型描述文本 + 全部传感器实时读数 + 告警状态
 */
static void lcd_display(const system_state_t *s)
{
    /* TODO: 对接 LCD 驱动（帧缓冲 / SPI 屏等）；
     *       开发阶段用标准输出代替，便于调试。 */
    printf("\033[2J\033[H");   /* 清屏，模拟 LCD 整屏刷新 */
    printf("======== 智能粮仓检测系统 ========\n");
    printf("巡检状态 : %s\n", s->inspection_running ? "运行中" : "已暂停");
    printf("供电电压 : %s%.2f V\n", s->voltage_valid ? "" : "ERR ", s->voltage);
    printf("负载电流 : %.2f A\n", s->current);
    printf("实时功率 : %.2f W (P=U*I)\n", s->power);
    printf("仓内温度 : %s%.1f ℃\n", s->ht_valid ? "" : "ERR ", s->temperature);
    printf("仓内湿度 : %s%.1f %%RH\n", s->ht_valid ? "" : "ERR ", s->humidity);
    printf("光照强度 : %s%.0f lux\n", s->light_valid ? "" : "ERR ", s->light);
    printf("人体红外 : %s\n", s->person_detected ? "有人闯入!" : "无人");
    printf("火焰传感 : %s\n", s->fire_detected ? "检测到明火!" : "正常");
    printf("AI 识别  : %s\n", s->ai_valid ? s->ai.description : "等待识别...");
    printf("告警状态 : %s\n", alarm_name(s->alarm));
}

/**
 * @brief 局域网网页推流刷新：叠加传感器实时数据、AI 结果与告警文字
 */
static void web_stream_update(const system_state_t *s)
{
    /* TODO: 对接网页视频服务（如 MJPEG streamer），将以下数据以文本
     *       叠加到画面：电压/电流/功率、温湿度、光照、AI 识别结果、
     *       告警码（E01~E05）。 */
    (void)s;
}

/**
 * @brief 局域网推流启停控制：巡检暂停时同步暂停推流
 */
static void web_stream_set_running(bool running)
{
    /* TODO: 对接推流服务；running=false 时停止向网页推送画面。 */
    (void)running;
}

/**
 * @brief 告警时自动抓拍当前画面，保存为 JPG 图片到本地（事后追溯）
 * @return 0 成功，-1 失败
 */
static int snapshot_save_jpg(void)
{
    /* TODO: 调用摄像头抓拍一帧，以时间戳命名保存为 .jpg 文件。 */
    printf("[抓拍] 保存告警现场图片（桩函数）\n");
    return -1;
}

/**
 * @brief 人体红外传感器：返回 true 表示检测到有人闯入
 * TODO: 需要 GPIO 驱动程序，建议新增 driver/pir_sensor.c 后对接
 */
static bool pir_read(void)
{
    return false;
}

/**
 * @brief 火焰传感器：返回 true 表示检测到明火
 * TODO: 需要 GPIO 驱动程序，建议新增 driver/flame_sensor.c 后对接
 */
static bool flame_read(void)
{
    return false;
}

/* ============================================================================
 * 三、传感器采集
 * ========================================================================== */

/**
 * @brief 读取电压传感器并换算为系统供电电压
 *        传感器模拟输出 0~3V 对应真实供电 0~6V：真实电压 = ADC 电压 × 2
 * @return true 成功；false 失败（调用方应标记数据无效）
 */
static bool voltage_read_system(float *volts)
{
    float millivolts = 0.0f;

    if (voltage_sensor_read(&millivolts) < 0) {
        return false;
    }
    *volts = millivolts / 1000.0f * VOLTAGE_CONVERT_RATIO;
    return true;
}

/**
 * @brief 传感器轮询任务（由传感器定时节拍触发）
 *        依次采集电压、温湿度、光照、人体红外、火焰数据并更新全局状态。
 *        sysfs 读取放在锁外进行，避免长时间持有状态锁。
 */
static void sensor_poll_task(void)
{
    float voltage = 0.0f, temperature = 0.0f, humidity = 0.0f, light = 0.0f;
    bool voltage_ok, ht_ok, light_ok, person, fire;

    /* 1. 采集各传感器（失败时保留错误值，由有效标志屏蔽后续告警判定） */
    voltage_ok  = voltage_read_system(&voltage);
    temperature = ht_read_temperature();
    humidity    = ht_read_humidity();
    ht_ok       = (temperature != HT_SENSOR_ERROR_VALUE &&
                   humidity != HT_SENSOR_ERROR_VALUE);
    light       = light_read();
    light_ok    = (light != LIGHT_SENSOR_ERROR_VALUE);
    person      = pir_read();    /* 人体红外（桩函数，待接 GPIO 驱动） */
    fire        = flame_read();  /* 火焰传感器（桩函数，待接 GPIO 驱动） */

    /* 2. 加锁更新全局状态 */
    pthread_mutex_lock(&g_state_mutex);
    g_state.voltage         = voltage;
    g_state.voltage_valid   = voltage_ok;
    g_state.temperature     = temperature;
    g_state.humidity        = humidity;
    g_state.ht_valid        = ht_ok;
    g_state.light           = light;
    g_state.light_valid     = light_ok;
    g_state.person_detected = person;
    g_state.fire_detected   = fire;
    pthread_mutex_unlock(&g_state_mutex);
}

/* ============================================================================
 * 四、功率计算
 * ========================================================================== */

/**
 * @brief 根据当前开启的外设模拟负载电流，计算整机实时功率 P = U × I
 *        电流规则：基础待机 0.10A；风扇 +0.15A；蜂鸣器鸣叫 +0.08A；
 *                  LED 全部点亮 / 闪烁 +0.05A
 * 注意：调用方必须持有 g_state_mutex。
 */
static void update_power_locked(void)
{
    system_state_t *s = &g_state;
    float current = CURRENT_BASE_STANDBY;

    if (s->fan_on)          current += CURRENT_FAN;
    if (s->buzzer_sounding) current += CURRENT_BUZZER;
    if (s->led_all_on)      current += CURRENT_LED_ALL;

    s->current = current;
    s->power   = s->voltage_valid ? (s->voltage * current) : 0.0f;
}

/* ============================================================================
 * 五、告警判定
 * ========================================================================== */

/**
 * @brief 综合传感器数据与 AI 识别结果，判定当前告警码
 *        优先级：E05 明火 > E03 人员闯入 > E04 粮情异常
 *                > E01 电压异常 > E02 温湿度
 * 注意：调用方必须持有 g_state_mutex（只读全局状态）。
 */
static alarm_code_t check_alarms(const system_state_t *s)
{
    /* E05 明火：火焰传感器 或 AI 识别到明火隐患 */
    if (s->fire_detected || (s->ai_valid && s->ai.fire_hazard)) {
        return ALARM_E05_FIRE;
    }
    /* E03 人员非法闯入：人体红外传感器 或 AI 识别到人员 */
    if (s->person_detected || (s->ai_valid && s->ai.person_intrusion)) {
        return ALARM_E03_PERSON;
    }
    /* E04 粮情异常：霉变结块 / 粮面塌陷 / 虫害堆积 / 杂物遗留 */
    if (s->ai_valid && (s->ai.grain_mold || s->ai.grain_collapse ||
                        s->ai.pest_infestation || s->ai.debris_left)) {
        return ALARM_E04_GRAIN;
    }
    /* E01 供电电压异常：欠压(<4.8V) 或 过压(>5.2V) */
    if (s->voltage_valid &&
        (s->voltage < VOLTAGE_SAFE_MIN || s->voltage > VOLTAGE_SAFE_MAX)) {
        return ALARM_E01_VOLTAGE;
    }
    /* E02 温湿度超出储粮安全阈值 */
    if (s->ht_valid &&
        (s->temperature > TEMPERATURE_MAX || s->humidity > HUMIDITY_MAX)) {
        return ALARM_E02_TEMP_HUMID;
    }
    return ALARM_NONE;
}

/**
 * @brief 告警判定任务：传感器数据或 AI 结果更新后调用，刷新全局告警码
 */
static void alarm_evaluate_task(void)
{
    alarm_code_t code;

    pthread_mutex_lock(&g_state_mutex);
    code = check_alarms(&g_state);
    if (code != g_state.alarm) {
        g_state.alarm = code;
        printf("[告警] %s\n", alarm_name(code));
    }
    pthread_mutex_unlock(&g_state_mutex);
}

/* ============================================================================
 * 六、分级告警执行
 * ========================================================================== */

/**
 * @brief 带状态缓存的 LED 控制：目标状态未变化时不重复写 sysfs，降低 IO 开销
 */
static void led_set_cached(LED_Device_ID id, LED_Status status)
{
    static LED_Status last_status[LED_DEVICE_ID_MAX] = {
        LED_OFF, LED_OFF, LED_OFF, LED_OFF
    };

    if (id < LED_DEVICE_ID_1 || id >= LED_DEVICE_ID_MAX) {
        return;
    }
    if (last_status[id] != status) {
        ledcontrol(id, status);
        last_status[id] = status;
    }
}

/**
 * @brief 带状态缓存的蜂鸣器控制：频率未变化时不重复发送输入事件
 */
static void buzzer_set_cached(unsigned int frequency_hz)
{
    static unsigned int last_frequency = 0xFFFFFFFFU; /* 初始无效值，确保首次必写 */

    if (frequency_hz != last_frequency) {
        buzzer_set(frequency_hz);
        last_frequency = frequency_hz;
    }
}

/**
 * @brief 分级告警执行任务（主循环每次迭代调用，周期 MAIN_LOOP_PERIOD_MS）
 *        - E01        ：LED1 常亮 + 蜂鸣器低频间断鸣响
 *        - E02        ：LED2 常亮 + 风扇自动通风降湿
 *        - E03/E04/E05：LED1~LED3 交替闪烁 + 蜂鸣器高频持续报警
 *        - 告警触发沿（无告警 → 有告警）时自动抓拍保存 JPG 图片
 *        - 告警码变化时立即刷新局域网网页叠加提示
 *        - 依据执行机构状态实时刷新负载电流与功率 P = U × I
 */
static void alarm_output_task(void)
{
    static unsigned int phase = 0;        /* 节拍计数器，驱动间断 / 闪烁节奏 */
    static bool web_overlay_dirty = true; /* 网页叠加信息需要刷新 */
    system_state_t *s = &g_state;
    alarm_code_t alarm;
    bool muted;

    phase++;

    pthread_mutex_lock(&g_state_mutex);
    alarm = s->alarm;
    muted = s->buzzer_muted;

    /* ---- 1. LED 分级灯光指示 ---- */
    switch (alarm) {
    case ALARM_E01_VOLTAGE:
        /* 电压告警：LED1 常亮指示 */
        led_set_cached(LED_DEVICE_ID_1, LED_ON);
        led_set_cached(LED_DEVICE_ID_2, LED_OFF);
        led_set_cached(LED_DEVICE_ID_3, LED_OFF);
        s->led_all_on = false;
        break;

    case ALARM_E02_TEMP_HUMID:
        /* 温湿度告警：LED2 常亮指示 */
        led_set_cached(LED_DEVICE_ID_1, LED_OFF);
        led_set_cached(LED_DEVICE_ID_2, LED_ON);
        led_set_cached(LED_DEVICE_ID_3, LED_OFF);
        s->led_all_on = false;
        break;

    case ALARM_E03_PERSON:
    case ALARM_E04_GRAIN:
    case ALARM_E05_FIRE:
        /* 高级别告警：LED1~LED3 交替闪烁（每个相位轮流点亮一颗） */
        {
            unsigned int step  = LED_BLINK_PHASE_MS / MAIN_LOOP_PERIOD_MS;
            unsigned int which = (phase / step) % 3;
            led_set_cached(LED_DEVICE_ID_1, which == 0 ? LED_ON : LED_OFF);
            led_set_cached(LED_DEVICE_ID_2, which == 1 ? LED_ON : LED_OFF);
            led_set_cached(LED_DEVICE_ID_3, which == 2 ? LED_ON : LED_OFF);
            s->led_all_on = true;   /* 三灯轮流点亮，按规则计入 +0.05A */
        }
        break;

    case ALARM_NONE:
    default:
        /* 无告警：全部熄灭 */
        led_set_cached(LED_DEVICE_ID_1, LED_OFF);
        led_set_cached(LED_DEVICE_ID_2, LED_OFF);
        led_set_cached(LED_DEVICE_ID_3, LED_OFF);
        s->led_all_on = false;
        break;
    }

    /* ---- 2. 蜂鸣器分级声音告警（KEY3 静音时一律关闭） ---- */
    if (muted) {
        buzzer_set_cached(BUZZER_OFF_FREQUENCY);
        s->buzzer_sounding = false;
    } else if (alarm == ALARM_E03_PERSON || alarm == ALARM_E04_GRAIN ||
               alarm == ALARM_E05_FIRE) {
        buzzer_set_cached(BUZZER_HIGH_FREQ);   /* 高频持续报警 */
        s->buzzer_sounding = true;
    } else if (alarm == ALARM_E01_VOLTAGE) {
        /* 低频间断鸣响：鸣 BUZZER_E01_ON_MS → 静 BUZZER_E01_OFF_MS，循环 */
        unsigned int cycle = (BUZZER_E01_ON_MS + BUZZER_E01_OFF_MS) /
                             MAIN_LOOP_PERIOD_MS;
        bool on = (phase % cycle) < (BUZZER_E01_ON_MS / MAIN_LOOP_PERIOD_MS);
        buzzer_set_cached(on ? BUZZER_LOW_FREQ : BUZZER_OFF_FREQUENCY);
        s->buzzer_sounding = on;
    } else {
        buzzer_set_cached(BUZZER_OFF_FREQUENCY);
        s->buzzer_sounding = false;
    }

    /* ---- 3. 风扇：仅温湿度超标（E02）时通风降湿 ---- */
    if (alarm == ALARM_E02_TEMP_HUMID && !s->fan_on) {
        fan_set(FAN_ON_PWM);
        s->fan_on = true;
    } else if (alarm != ALARM_E02_TEMP_HUMID && s->fan_on) {
        fan_set(FAN_OFF_PWM);
        s->fan_on = false;
    }

    /* ---- 4. 执行机构状态已更新，重新计算电流与功率 ---- */
    update_power_locked();

    /* ---- 5. 告警沿处理：自动抓拍 + 标记网页刷新 ---- */
    if (alarm != s->last_alarm) {
        if (alarm == ALARM_E01_VOLTAGE || alarm == ALARM_E03_PERSON ||
            alarm == ALARM_E04_GRAIN || alarm == ALARM_E05_FIRE) {
            snapshot_save_jpg();   /* E01 / E03~E05：自动抓拍保存 JPG */
        }
        s->last_alarm = alarm;
        web_overlay_dirty = true;  /* 网页需要叠加新的告警文字 */
    }

    pthread_mutex_unlock(&g_state_mutex);

    /* ---- 6. 告警码变化时，立即刷新局域网网页叠加提示 ---- */
    if (web_overlay_dirty) {
        pthread_mutex_lock(&g_state_mutex);
        web_stream_update(&g_state);
        pthread_mutex_unlock(&g_state_mutex);
        web_overlay_dirty = false;
    }
}

/**
 * @brief 告警待机：巡检暂停时关闭全部告警输出并清空告警状态
 */
static void alarm_standby(void)
{
    pthread_mutex_lock(&g_state_mutex);
    g_state.alarm          = ALARM_NONE;
    g_state.last_alarm     = ALARM_NONE;
    g_state.fan_on         = false;
    g_state.buzzer_sounding = false;
    g_state.led_all_on     = false;
    pthread_mutex_unlock(&g_state_mutex);

    /* 经缓存层操作执行机构，确保立即安静且缓存状态保持一致 */
    led_set_cached(LED_DEVICE_ID_1, LED_OFF);
    led_set_cached(LED_DEVICE_ID_2, LED_OFF);
    led_set_cached(LED_DEVICE_ID_3, LED_OFF);
    buzzer_set_cached(BUZZER_OFF_FREQUENCY);
    fan_set(FAN_OFF_PWM);
}

/* ============================================================================
 * 七、摄像头采集与 AI 识别
 * ========================================================================== */

/**
 * @brief 摄像头采集 + AI 识别任务：由摄像头定时节拍或 KEY2 手动触发
 *        流程：采集画面 → Base64 编码 → 多模态大模型识别 → 更新 AI 结果
 */
static void camera_ai_task(void)
{
    static char image_base64[AI_IMAGE_BUFFER_SIZE]; /* Base64 图像缓冲区 */
    ai_result_t result;

    /* 1. 采集一帧粮仓画面并做 Base64 编码 */
    if (camera_capture_base64(image_base64, sizeof(image_base64)) < 0) {
        /* 摄像头尚未接入：保留上一次 AI 识别结果 */
        return;
    }

    /* 2. 送入多模态大模型识别画面内容 */
    if (ai_analyze_image(image_base64, &result) < 0) {
        return;   /* 识别失败：保留上一次结果 */
    }

    /* 3. 更新全局 AI 识别结果 */
    pthread_mutex_lock(&g_state_mutex);
    g_state.ai = result;
    g_state.ai_valid = true;
    pthread_mutex_unlock(&g_state_mutex);
}

/* ============================================================================
 * 八、显示与推流刷新
 * ========================================================================== */

/**
 * @brief 显示刷新任务：LCD 屏幕 + 局域网网页，按 LCD_REFRESH_PERIOD_MS
 *        周期刷新；KEY2 手动抓拍时可传 force=true 立即刷新 AI 识别结果
 */
static void display_refresh_task(bool force)
{
    static unsigned int tick = 0;          /* 主循环节拍计数 */
    static unsigned int last_refresh = 0;  /* 上次刷新时的节拍数 */

    tick++;
    if (!force &&
        (tick - last_refresh) < (LCD_REFRESH_PERIOD_MS / MAIN_LOOP_PERIOD_MS)) {
        return;
    }
    last_refresh = tick;

    pthread_mutex_lock(&g_state_mutex);
    lcd_display(&g_state);        /* LCD：传感器数据 + AI 描述 + 告警状态 */
    web_stream_update(&g_state);  /* 网页：实时数据 + AI 结果 + 告警文字叠加 */
    pthread_mutex_unlock(&g_state_mutex);
}

/* ============================================================================
 * 九、按键处理
 * ========================================================================== */

/**
 * @brief KEY1：启动 / 暂停粮仓巡检任务
 *        启动：恢复传感器轮询与摄像头采集节拍，恢复画面推流
 *        暂停：停止周期采集与轮询，告警系统待机，推流同步暂停
 */
static void inspection_toggle(void)
{
    bool running;

    pthread_mutex_lock(&g_state_mutex);
    g_state.inspection_running = !g_state.inspection_running;
    running = g_state.inspection_running;
    pthread_mutex_unlock(&g_state_mutex);

    if (running) {
        my_timer_start(g_sensor_timer);
        my_timer_start(g_camera_timer);
        web_stream_set_running(true);
        printf("[巡检] 任务启动：周期采集 + AI 识别 + 推流\n");
    } else {
        my_timer_stop(g_sensor_timer);
        my_timer_stop(g_camera_timer);
        web_stream_set_running(false);
        alarm_standby();
        printf("[巡检] 任务暂停：采集 / 轮询 / 告警 / 推流已停止\n");
    }
}

/**
 * @brief KEY2：手动抓拍粮仓图像，立即提交大模型分析并刷新屏幕
 */
static void manual_capture(void)
{
    camera_ai_task();            /* 抓拍 + Base64 + AI 即时分析 */
    display_refresh_task(true);  /* 屏幕立即刷新 AI 识别结果 */
}

/**
 * @brief KEY3：蜂鸣器静音开关
 *        静音后关闭声音告警；灯光告警与局域网画面告警提示继续保留
 */
static void buzzer_mute_toggle(void)
{
    bool muted;

    pthread_mutex_lock(&g_state_mutex);
    g_state.buzzer_muted = !g_state.buzzer_muted;
    muted = g_state.buzzer_muted;
    pthread_mutex_unlock(&g_state_mutex);

    if (muted) {
        buzzer_set_cached(BUZZER_OFF_FREQUENCY);   /* 立即静音 */
    }
    printf("[按键] 蜂鸣器%s\n", muted ? "已静音" : "已取消静音");
}

/**
 * @brief 按键处理任务：非阻塞扫描按键输入队列并分发处理
 *        只响应“按下”事件，忽略抬起与长按重复，避免误触发
 */
static void key_task(void)
{
    key_event_t event;

    /* 一次性取空事件队列 */
    while (key_read_event(&g_key, &event) == 1) {
        if (event.type != KEY_EVENT_PRESS) {
            continue;
        }
        printf("[按键] %s 按下\n", key_get_name(event.code));
        switch (event.code) {
        case KEY1_CODE: inspection_toggle();  break;  /* 巡检启动 / 暂停 */
        case KEY2_CODE: manual_capture();     break;  /* 手动抓拍 + AI 分析 */
        case KEY3_CODE: buzzer_mute_toggle(); break;  /* 蜂鸣器静音开关 */
        default: break;   /* 其他按键忽略 */
        }
    }
}

/* ============================================================================
 * 十、定时器回调
 * ----------------------------------------------------------------------------
 * 回调运行在定时器线程中，只负责置位“节拍”标志；
 * 具体采集、识别、告警等工作由主循环消费标志后完成，
 * 避免多线程并发操作外设与共享状态。
 * ========================================================================== */

static void sensor_timer_callback(void)
{
    g_sensor_tick = 1;
}

static void camera_timer_callback(void)
{
    g_camera_tick = 1;
}

/* ============================================================================
 * 十一、信号处理与主函数
 * ========================================================================== */

/* SIGINT / SIGTERM 处理：请求退出主循环
 * （delay_ms 使用的 nanosleep 被信号中断后，循环随即退出） */
static void on_terminate_signal(int signo)
{
    (void)signo;
    g_running = 0;
}

int main(void)
{
    /* ---- 1. 注册退出信号，保证 Ctrl-C 时能够清理外设 ---- */
    signal(SIGINT, on_terminate_signal);
    signal(SIGTERM, on_terminate_signal);

    /* ---- 2. 初始化各外设驱动：按键 / LED / 蜂鸣器 / 风扇 ---- */
    if (key_init_auto(&g_key) < 0) {
        fprintf(stderr, "[初始化] 按键设备打开失败，按键功能不可用\n");
    }
    ledcontrol(LED_DEVICE_ID_1, LED_OFF);
    ledcontrol(LED_DEVICE_ID_2, LED_OFF);
    ledcontrol(LED_DEVICE_ID_3, LED_OFF);
    buzzer_set(BUZZER_OFF_FREQUENCY);
    fan_set(FAN_OFF_PWM);

    /* ---- 3. 创建定时任务（创建即启动）----
     *    传感器轮询 1s 一次；摄像头采集 / AI 识别 5s 一次 */
    g_sensor_timer = my_timer_create(SENSOR_POLL_PERIOD_MS,
                                     sensor_timer_callback);
    g_camera_timer = my_timer_create(CAMERA_CAPTURE_PERIOD_MS,
                                     camera_timer_callback);
    if (g_sensor_timer < 0 || g_camera_timer < 0) {
        fprintf(stderr, "[初始化] 定时器创建失败\n");
    }

    /* 初始为“暂停”态：等待 KEY1 启动巡检，先停掉两个定时器 */
    my_timer_stop(g_sensor_timer);
    my_timer_stop(g_camera_timer);

    printf("[系统] 智能粮仓检测系统初始化完成\n");
    printf("[系统] 按下 KEY1 启动粮仓巡检任务\n");

    /* ---- 4. 主循环：按功能说明文档第五部分逻辑链顺序执行 ---- */
    while (g_running) {
        /* 巡检运行中才消费定时节拍：暂停时停止周期采集与轮询 */
        if (g_state.inspection_running) {
            if (g_sensor_tick) {          /* 传感器轮询节拍 */
                g_sensor_tick = 0;
                sensor_poll_task();
                alarm_evaluate_task();
            }
            if (g_camera_tick) {          /* 摄像头采集 / AI 识别节拍 */
                g_camera_tick = 0;
                camera_ai_task();
                alarm_evaluate_task();    /* AI 结果更新后重新判定告警 */
            }
        }

        key_task();                 /* 按键扫描：KEY1 启停 / KEY2 抓拍 / KEY3 静音 */
        alarm_output_task();        /* 分级告警执行：LED / 蜂鸣器 / 风扇 / 抓拍 */
        display_refresh_task(false);/* LCD + 局域网网页数据刷新 */

        delay_ms(MAIN_LOOP_PERIOD_MS);
    }

    /* ---- 5. 退出清理：销毁定时器、关闭外设 ---- */
    printf("\n[系统] 正在退出...\n");
    if (g_sensor_timer >= 0) {
        my_timer_destroy(g_sensor_timer);
    }
    if (g_camera_timer >= 0) {
        my_timer_destroy(g_camera_timer);
    }
    alarm_standby();
    web_stream_set_running(false);
    key_close(&g_key);
    printf("[系统] 已安全退出\n");
    return 0;
}
