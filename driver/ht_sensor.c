/*
 * ht_sensor.c —— 温湿度传感器驱动实现（IIO 子系统）
 *
 * 实现流程：
 *   1. find_iio_device 以湿度原始值为特征定位 IIO 设备目录；
 *   2. 分别读取温度 / 湿度通道的 raw + offset + scale；
 *   3. 换算公式：物理值 = (raw + offset) × scale / 1000
 *      （IIO 温度为毫摄氏度、湿度为毫百分比，除以 1000 后分别得到
 *        ℃ 与 %RH）；
 *   4. 任一环节失败统一返回 HT_SENSOR_ERROR_VALUE(-999.0f)。
 */

#include "ht_sensor.h"
#include "file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 从 sysfs 属性文件读取一个浮点数
 * @param path  属性文件路径
 * @param value 输出：解析出的浮点值
 * @return 成功 0；失败 -1
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
 * @brief 读取一个 IIO 通道并换算为物理值
 * @param raw_name   原始值属性文件名
 * @param offset_name 偏移量属性文件名
 * @param scale_name  缩放系数属性文件名
 * @return 物理值（℃ 或 %RH）；失败返回 HT_SENSOR_ERROR_VALUE
 */
static float read_channel(const char *raw_name, const char *offset_name,
                          const char *scale_name)
{
    char directory[128];
    char path[192];
    float raw;
    float offset;
    float scale;
    int path_length;

    /* 1. 定位 IIO 设备目录（以湿度原始值作为特征文件） */
    if (find_iio_device(HT_SENSOR_MARKER,
                        directory, (int)sizeof(directory)) < 0) {
        return HT_SENSOR_ERROR_VALUE;
    }

    /* 2. 读取原始采样值 */
    path_length = snprintf(path, sizeof(path), "%s/%s", directory, raw_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
        read_float_file(path, &raw) < 0) {
        return HT_SENSOR_ERROR_VALUE;
    }
    /* 3. 读取偏移量 */
    path_length = snprintf(path, sizeof(path), "%s/%s", directory, offset_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
        read_float_file(path, &offset) < 0) {
        return HT_SENSOR_ERROR_VALUE;
    }
    /* 4. 读取缩放系数 */
    path_length = snprintf(path, sizeof(path), "%s/%s", directory, scale_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
        read_float_file(path, &scale) < 0) {
        return HT_SENSOR_ERROR_VALUE;
    }

    /* 5. 换算：毫摄氏度 / 毫百分比 → ℃ / %RH */
    return (raw + offset) * scale / 1000.0f;
}

/**
 * @brief 读取仓内温度（℃）
 */
float ht_read_temperature(void)
{
    return read_channel(HT_SENSOR_TEMPERATURE_RAW,
                        HT_SENSOR_TEMPERATURE_OFFSET,
                        HT_SENSOR_TEMPERATURE_SCALE);
}

/**
 * @brief 读取仓内相对湿度（%RH）
 */
float ht_read_humidity(void)
{
    return read_channel(HT_SENSOR_HUMIDITY_RAW,
                        HT_SENSOR_HUMIDITY_OFFSET,
                        HT_SENSOR_HUMIDITY_SCALE);
}
