/*
 * ht_sensor.h —— 温湿度传感器驱动（IIO 子系统）
 *
 * 【硬件原理】
 *   温湿度传感器经 FS-MP1A 的 IIO 子系统暴露两个通道：
 *     - 温度：in_temp_raw + in_temp_offset + in_temp_scale
 *     - 湿度：in_humidityrelative_raw + in_humidityrelative_offset
 *             + in_humidityrelative_scale
 *   IIO 惯例：温度为毫摄氏度、湿度为毫百分比，因此驱动内部统一
 *   除以 1000，直接输出 ℃ 与 %RH。
 *
 * 【定位方式】以 "in_humidityrelative_raw" 作为特征文件，
 *   通过 find_iio_device 在 iio:deviceN 中探测设备目录。
 *
 * 【错误处理】任一环节失败返回 HT_SENSOR_ERROR_VALUE(-999.0f)，
 *   上层据此标记数据无效，屏蔽告警判定。
 */

#ifndef HT_SENSOR_H
#define HT_SENSOR_H

/* 用于定位 IIO 设备的特征属性文件（湿度原始值一定存在） */
#define HT_SENSOR_MARKER "in_humidityrelative_raw"
/* 读取失败时返回的错误值 */
#define HT_SENSOR_ERROR_VALUE (-999.0f)
/* 温度通道属性文件名 */
#define HT_SENSOR_TEMPERATURE_RAW "in_temp_raw"
#define HT_SENSOR_TEMPERATURE_OFFSET "in_temp_offset"
#define HT_SENSOR_TEMPERATURE_SCALE "in_temp_scale"
/* 湿度通道属性文件名 */
#define HT_SENSOR_HUMIDITY_RAW "in_humidityrelative_raw"
#define HT_SENSOR_HUMIDITY_OFFSET "in_humidityrelative_offset"
#define HT_SENSOR_HUMIDITY_SCALE "in_humidityrelative_scale"

/**
 * @brief 读取仓内温度
 * @return 温度值（℃）；失败返回 HT_SENSOR_ERROR_VALUE(-999.0f)
 */
float ht_read_temperature(void);

/**
 * @brief 读取仓内湿度
 * @return 相对湿度（%RH）；失败返回 HT_SENSOR_ERROR_VALUE(-999.0f)
 */
float ht_read_humidity(void);

#endif
