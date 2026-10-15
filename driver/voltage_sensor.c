/*
 * voltage_sensor.c —— 可调档位电压传感器驱动实现（IIO ADC 通道）
 *
 * 实现流程：
 *   1. find_iio_device 定位 ADC 的 IIO 设备目录；
 *   2. 读取 in_voltageN_raw 得到原始采样值；
 *   3. 读取 in_voltage_scale / in_voltage_offset 换算参数
 *      （缺失时用默认量程 3.3V/65536 兜底）；
 *   4. 实际电压(mV) = (raw + offset) × scale。
 */

#include "voltage_sensor.h"
#include "file.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief 从 sysfs 属性文件读取一个浮点数
 * @param path  属性文件路径
 * @param value 输出：解析出的浮点值
 * @return 成功 0；失败 -1
 *
 * 说明：读取后用 strtof 解析，并严格校验整个字符串均为有效数字
 *       （允许尾部空白），防止 sysfs 返回异常内容导致误判。
 */
static int read_float_file(const char *path, float *value)
{
    char buf[64];
    char *end;
    float parsed;

    if (read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }

    errno = 0;
    parsed = strtof(buf, &end);
    if (end == buf || errno == ERANGE) {
        return -1;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return -1;
    }

    *value = parsed;
    return 0;
}

/**
 * @brief 读取指定 ADC 通道的电压值（单位 mV）
 * @param channel     ADC 通道
 * @param millivolts  输出：电压毫伏值
 * @return 成功 0；失败 -1
 */
int voltage_read(Voltage_Channel channel, float *millivolts)
{
    char directory[128];
    char path[192];
    float raw;
    float scale = VOLTAGE_SENSOR_DEFAULT_SCALE_MV;
    float offset = 0.0f;
    int path_length;

    /* 参数校验 + 定位 IIO 设备目录 */
    if (millivolts == NULL || channel < VOLTAGE_CHANNEL_0 ||
        channel >= VOLTAGE_CHANNEL_MAX ||
        find_iio_device(VOLTAGE_SENSOR_RAW_MARKER,
                        directory, (int)sizeof(directory)) < 0) {
        return -1;
    }

    /* 1. 读取原始采样值 in_voltageN_raw */
    path_length = snprintf(path, sizeof(path), VOLTAGE_SENSOR_RAW_PATH_FORMAT,
                           directory, (int)channel);
    if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
        read_float_file(path, &raw) < 0) {
        return -1;
    }

    /* 2. 读取缩放系数 in_voltage_scale（缺失时用默认值兜底） */
    path_length = snprintf(path, sizeof(path),
                           VOLTAGE_SENSOR_SCALE_PATH_FORMAT, directory);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        return -1;
    }
    if (read_float_file(path, &scale) < 0) {
        scale = VOLTAGE_SENSOR_DEFAULT_SCALE_MV;
    }

    /* 3. 读取偏移量 in_voltage_offset（缺失时按 0 处理） */
    path_length = snprintf(path, sizeof(path),
                           VOLTAGE_SENSOR_OFFSET_PATH_FORMAT, directory);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        return -1;
    }
    if (read_float_file(path, &offset) < 0) {
        offset = 0.0f;
    }

    /* 4. 换算实际电压：mV = (raw + offset) × scale */
    *millivolts = (raw + offset) * scale;
    return 0;
}

/**
 * @brief 读取电流测量通道（通道 0，预留）
 */
int current_read(float *millivolts)
{
    return voltage_read(VOLTAGE_CHANNEL_0, millivolts);
}

/**
 * @brief 读取可调档位电压传感器（通道 1）
 *        返回传感器模拟输出对应的毫伏值，真实供电电压需再 ×2
 *        （见 main.c voltage_read_system）
 */
int voltage_sensor_read(float *millivolts)
{
    return voltage_read(VOLTAGE_CHANNEL_1, millivolts);
}
