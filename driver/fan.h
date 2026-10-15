/*
 * fan.h —— 直流风扇驱动（hwmon 子系统 PWM 控制）
 *
 * 【硬件原理】
 *   风扇由开发板的 PWM 控制器驱动，Linux 中以 hwmon 设备暴露：
 *     - /sys/class/hwmon/hwmonN/name   设备名称（本平台为 "pwmfan"）
 *     - /sys/class/hwmon/hwmonN/pwm1   占空比控制（0~255）
 *   hwmon 设备编号 N 不固定，因此驱动启动时扫描 hwmon0~hwmon7，
 *   以 name 文件内容前缀 "pwmfan" 定位真正的风扇设备。
 *
 * 【本项目用途】
 *   温湿度超出储粮安全阈值（E02）时全速运转通风降湿；
 *   告警解除后关闭。
 */

#ifndef FAN_H
#define FAN_H

/* hwmon 设备名称文件路径格式（用于定位风扇设备） */
#define FAN_HWMON_NAME_PATH_FORMAT "/sys/class/hwmon/hwmon%d/name"
/* 风扇 PWM 控制文件路径格式 */
#define FAN_PWM_PATH_FORMAT "/sys/class/hwmon/hwmon%d/pwm1"
/* 扫描 hwmon 设备的最大数量 */
#define FAN_HWMON_DEVICE_COUNT 8
/* 风扇 hwmon 设备名称前缀 */
#define FAN_NAME_PREFIX "pwmfan"
/* PWM 最大值（对应全速运转） */
#define FAN_PWM_MAX 255U

/**
 * @brief 设置风扇转速（PWM 占空比）
 * @param pwm 占空比 0（停转）~ FAN_PWM_MAX（全速）
 * @return 成功 0；失败 -1
 */
int fan_set(unsigned int pwm);

#endif
