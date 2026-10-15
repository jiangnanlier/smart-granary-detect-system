/*
 * light_sensor.h —— 光照传感器驱动（IIO 子系统）
 *
 * 【硬件原理】
 *   光照 / 接近传感器经 FS-MP1A 的 IIO 子系统暴露多个属性：
 *     - in_illuminance_input ：可见光光照强度（lux），本项目使用；
 *     - in_illuminance_ir_raw：红外光强原始值；
 *     - in_proximity_raw     ：接近感应原始值。
 *   各属性所在设备目录通过 find_iio_device 以对应属性名探测定位。
 *
 * 【错误处理】任一环节失败返回 LIGHT_SENSOR_ERROR_VALUE(-1.0f)，
 *   上层据此标记数据无效。
 */

#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

/* 读取失败时返回的错误值 */
#define LIGHT_SENSOR_ERROR_VALUE (-1.0f)
/* 可见光光照强度属性（lux） */
#define LIGHT_SENSOR_VISIBLE_ATTRIBUTE "in_illuminance_input"
/* 红外光强属性 */
#define LIGHT_SENSOR_IR_ATTRIBUTE "in_illuminance_ir_raw"
/* 接近感应属性 */
#define LIGHT_SENSOR_PROXIMITY_ATTRIBUTE "in_proximity_raw"

/**
 * @brief 读取可见光光照强度
 * @return 光照强度（lux）；失败返回 LIGHT_SENSOR_ERROR_VALUE(-1.0f)
 */
float light_read(void);

/**
 * @brief 读取红外光强原始值
 * @return 原始值；失败返回 LIGHT_SENSOR_ERROR_VALUE(-1.0f)
 */
float light_read_ir(void);

/**
 * @brief 读取接近感应原始值
 * @return 原始值；失败返回 LIGHT_SENSOR_ERROR_VALUE(-1.0f)
 */
float light_read_proximity(void);

#endif
