/*
 * light_sensor.c —— 光照传感器驱动实现（IIO 子系统）
 *
 * 实现流程：
 *   1. find_iio_device 以目标属性名（如 in_illuminance_input）为特征，
 *      在 iio:deviceN 中探测设备目录；
 *   2. 读取该属性文件内容；
 *   3. strtof 解析为浮点数，任一环节失败返回
 *      LIGHT_SENSOR_ERROR_VALUE(-1.0f)。
 */

#include "light_sensor.h"
#include "file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 读取一个 IIO 属性并解析为浮点数
 * @param attribute IIO 属性文件名（同时用作设备定位特征）
 * @return 属性值；失败返回 LIGHT_SENSOR_ERROR_VALUE(-1.0f)
 */
static float read_iio_attribute(const char *attribute)
{
    char directory[128];
    char path[192];
    char buf[64];
    char *end;
    float value;
    int path_length;

    /* 1. 以该属性为特征定位 IIO 设备目录 */
    if (find_iio_device(attribute, directory, (int)sizeof(directory)) < 0) {
        return LIGHT_SENSOR_ERROR_VALUE;
    }

    /* 2. 拼接完整路径并读取属性内容 */
    path_length = snprintf(path, sizeof(path), "%s/%s", directory, attribute);
    if (path_length < 0 || (size_t)path_length >= sizeof(path) ||
        read_file(path, buf, sizeof(buf)) < 0) {
        return LIGHT_SENSOR_ERROR_VALUE;
    }

    /* 3. 解析浮点数（严格校验整个字符串均为有效数字） */
    errno = 0;
    value = strtof(buf, &end);
    if (end == buf || errno == ERANGE) {
        return LIGHT_SENSOR_ERROR_VALUE;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return LIGHT_SENSOR_ERROR_VALUE;
    }
    return value;
}

/**
 * @brief 读取可见光光照强度（lux）
 */
float light_read(void)
{
    return read_iio_attribute(LIGHT_SENSOR_VISIBLE_ATTRIBUTE);
}

/**
 * @brief 读取红外光强原始值
 */
float light_read_ir(void)
{
    return read_iio_attribute(LIGHT_SENSOR_IR_ATTRIBUTE);
}

/**
 * @brief 读取接近感应原始值
 */
float light_read_proximity(void)
{
    return read_iio_attribute(LIGHT_SENSOR_PROXIMITY_ATTRIBUTE);
}
