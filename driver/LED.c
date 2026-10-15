/*
 * LED.c —— LED 指示灯驱动实现（sysfs led class）
 *
 * 实现流程：
 *   1. 校验 LED 编号与目标状态合法性；
 *   2. 将目标状态映射为 sysfs 亮度值（"1"=点亮，"0"=熄灭）；
 *   3. 拼接 /sys/class/leds/ledN/brightness 路径；
 *   4. 调用 file.h 的 write_file 完成写入。
 */

#include "LED.h"
#include "file.h"

#include <stdio.h>

/**
 * @brief 控制一个 LED 的亮灭
 * @param lednum   LED 设备编号
 * @param ledstart 目标状态（LED_OFF / LED_ON）
 * @return 成功 0；失败 -1
 */
int ledcontrol(LED_Device_ID lednum, LED_Status ledstart)
{
    char path[64];
    int path_length;
    const char *brightness;

    /* 1. 参数校验：编号必须在 LED1~LED3 范围内 */
    if (lednum < LED_DEVICE_ID_1 || lednum >= LED_DEVICE_ID_MAX) {
        return -1;
    }

    /* 2. 目标状态 → sysfs 亮度字符串 */
    if (ledstart == LED_OFF) {
        brightness = "0";
    } else if (ledstart == LED_ON) {
        brightness = "1";
    } else {
        return -1;
    }

    /* 3. 拼接 sysfs 路径 */
    path_length = snprintf(path, sizeof(path), LED_BRIGHTNESS_PATH_FORMAT,
                           (int)lednum);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        return -1;
    }

    /* 4. 写入 brightness 文件，完成 LED 控制 */
    return write_file(path, brightness);
}
