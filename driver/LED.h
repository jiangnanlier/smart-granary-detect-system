/*
 * LED.h —— LED 指示灯驱动（sysfs led class）
 *
 * 【硬件原理】
 *   FS-MP1A 的 LED1/LED2/LED3 由 Linux led class 管理，
 *   通过 /sys/class/leds/ledN/brightness 文件控制：
 *     写 "1" → 点亮；写 "0" → 熄灭。
 *
 * 【本项目用途】（分级告警灯光指示）
 *   LED1 —— 电压告警指示（E01 时常亮）
 *   LED2 —— 温湿度告警指示（E02 时常亮）
 *   LED3 —— 参与高级别告警交替闪烁（E03/E04/E05 时 LED1~LED3 交替闪）
 */

#ifndef LED_H
#define LED_H

/* sysfs 亮度控制文件路径格式 */
#define LED_BRIGHTNESS_PATH_FORMAT "/sys/class/leds/led%d/brightness"

/* LED 目标状态 */
typedef enum {
    LED_OFF = 0,   /* 熄灭 */
    LED_ON = 1,    /* 点亮 */
} LED_Status;

/* LED 设备编号（对应 led1 / led2 / led3） */
typedef enum {
    LED_DEVICE_ID_1 = 1,   /* LED1：电压告警指示灯   */
    LED_DEVICE_ID_2,       /* LED2：温湿度告警指示灯 */
    LED_DEVICE_ID_3,       /* LED3：高级别告警闪烁   */
    LED_DEVICE_ID_MAX,
} LED_Device_ID;

/**
 * @brief 通过 sysfs brightness 接口控制一个 LED
 * @param lednum   LED 设备编号（LED_DEVICE_ID_1 ~ LED_DEVICE_ID_3）
 * @param ledstart 目标状态：LED_OFF 熄灭 / LED_ON 点亮
 * @return 成功 0；失败 -1
 */
int ledcontrol(LED_Device_ID lednum, LED_Status ledstart);

#endif
