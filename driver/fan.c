/*
 * fan.c —— 直流风扇驱动实现（hwmon 子系统 PWM 控制）
 *
 * 实现流程：
 *   1. find_fan_pwm_path()：扫描 hwmon0~hwmon7，读取各设备 name 文件，
 *      找到名称以 "pwmfan" 开头的设备，拼接其 pwm1 控制路径；
 *   2. fan_set()：校验占空比范围（0~255），定位设备路径，
 *      将占空比数值写入 pwm1 文件。
 */

#include "fan.h"
#include "file.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief 定位风扇 PWM 控制文件路径
 * @param path      输出：pwm1 文件完整路径
 * @param path_size path 缓冲区大小
 * @return 成功 0；失败 -1（未找到 pwmfan 设备）
 */
static int find_fan_pwm_path(char *path, size_t path_size)
{
    char name_path[96];
    char name[64];
    int i;
    int length;

    /* 遍历 hwmon0~hwmon7，按名称前缀识别风扇设备 */
    for (i = 0; i < FAN_HWMON_DEVICE_COUNT; i++) {
        length = snprintf(name_path, sizeof(name_path),
                          FAN_HWMON_NAME_PATH_FORMAT, i);
        if (length < 0 || (size_t)length >= sizeof(name_path)) {
            return -1;
        }
        /* 读取设备名称，跳过不存在或读不到的设备 */
        if (access(name_path, R_OK) != 0 ||
            read_file(name_path, name, sizeof(name)) < 0) {
            continue;
        }
        /* 名称前缀不匹配则跳过 */
        if (strncmp(name, FAN_NAME_PREFIX, strlen(FAN_NAME_PREFIX)) != 0) {
            continue;
        }

        /* 找到风扇设备，拼接 pwm1 控制路径 */
        length = snprintf(path, path_size, FAN_PWM_PATH_FORMAT, i);
        return (length < 0 || (size_t)length >= path_size) ? -1 : 0;
    }

    return -1;
}

/**
 * @brief 设置风扇转速（PWM 占空比）
 * @param pwm 0（停转）~ FAN_PWM_MAX（全速）
 * @return 成功 0；失败 -1
 */
int fan_set(unsigned int pwm)
{
    char path[96];
    char value[16];
    int length;

    /* 1. 占空比范围校验 */
    if (pwm > FAN_PWM_MAX) {
        return -1;
    }
    /* 2. 定位风扇 PWM 控制文件 */
    if (find_fan_pwm_path(path, sizeof(path)) < 0) {
        return -1;
    }

    /* 3. 将占空比数值转为字符串并写入 pwm1 */
    length = snprintf(value, sizeof(value), "%u", pwm);
    if (length < 0 || (size_t)length >= sizeof(value)) {
        return -1;
    }
    return write_file(path, value);
}
