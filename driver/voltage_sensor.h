/*
 * voltage_sensor.h —— 可调档位电压传感器驱动（IIO ADC 通道）
 *
 * 【硬件原理】
 *   可调档位电压传感器的模拟输出接 FS-MP1A 的 ADC 输入：
 *     - 硬件拨码切换 3 个档位，改变传感器输出的模拟电压
 *       （档位1 标准供电 2.50V / 档位2 欠压 2.30V / 档位3 过压 2.70V）；
 *     - 开发板 ADC 采样后，由 Linux IIO 子系统以
 *       in_voltageN_raw + in_voltage_offset + in_voltage_scale 暴露，
 *       实际电压(mV) = (raw + offset) × scale；
 *     - 传感器输出 0~3V 对应真实供电 0~6V，换算系数 ×2 由上层完成
 *       （见 main.c 的 voltage_read_system）。
 *
 * 【通道分配】
 *   - VOLTAGE_CHANNEL_0：电流测量通道（预留）
 *   - VOLTAGE_CHANNEL_1：电压传感器通道（本项目使用）
 *
 * 【单位】所有输出单位均为毫伏（mV）。
 */

#ifndef VOLTAGE_SENSOR_H
#define VOLTAGE_SENSOR_H

/* IIO 中用于定位 ADC 设备的特征属性文件 */
#define VOLTAGE_SENSOR_RAW_MARKER "in_voltage0_raw"
/* ADC 各通道原始值 / 缩放 / 偏移属性文件路径格式 */
#define VOLTAGE_SENSOR_RAW_PATH_FORMAT "%s/in_voltage%d_raw"
#define VOLTAGE_SENSOR_SCALE_PATH_FORMAT "%s/in_voltage_scale"
#define VOLTAGE_SENSOR_OFFSET_PATH_FORMAT "%s/in_voltage_offset"
/* 默认缩放：3.3V 参考电压 / 65536（16 位 ADC 精度），
 * 当 sysfs 未提供 in_voltage_scale 时兜底使用 */
#define VOLTAGE_SENSOR_DEFAULT_SCALE_MV (3300.0f / 65536.0f)

/* ADC 通道编号 */
typedef enum {
    VOLTAGE_CHANNEL_0 = 0,   /* 通道 0：电流测量（预留） */
    VOLTAGE_CHANNEL_1,       /* 通道 1：电压传感器       */
    VOLTAGE_CHANNEL_MAX,
} Voltage_Channel;

/**
 * @brief 读取指定 ADC 通道的电压值
 * @param channel     ADC 通道（VOLTAGE_CHANNEL_0 / VOLTAGE_CHANNEL_1）
 * @param millivolts  输出：电压值，单位毫伏(mV)
 * @return 成功 0；失败 -1
 */
int voltage_read(Voltage_Channel channel, float *millivolts);

/**
 * @brief 读取电流测量通道（通道 0，预留接口）
 * @param millivolts 输出：毫伏值
 * @return 成功 0；失败 -1
 */
int current_read(float *millivolts);

/**
 * @brief 读取可调档位电压传感器（通道 1）
 * @param millivolts 输出：传感器模拟输出对应的毫伏值（0~3000mV）
 * @return 成功 0；失败 -1
 */
int voltage_sensor_read(float *millivolts);

#endif
