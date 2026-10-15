/*
 * buzzer.c —— 蜂鸣器驱动实现（Linux input 子系统的 EV_SND 事件）
 *
 * 实现流程（buzzer_set）：
 *   1. 校验频率参数；
 *   2. 打开蜂鸣器输入设备；
 *   3. 构造 EV_SND / SND_TONE 输入事件（value = 频率）并写入；
 *   4. 关闭设备。
 *
 * buzzer_play_music 基于 buzzer_set 逐音符播放《小星星》旋律，
 * 每个音符之间插入 30ms 静音间隔，使音符清晰可辨。
 */

#include "buzzer.h"
#include "delay.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief 设置蜂鸣器音调（频率为 0 时停止鸣叫）
 * @param frequency_hz 鸣叫频率(Hz)
 * @return 成功 0；失败 -1
 */
int buzzer_set(unsigned int frequency_hz)
{
    struct input_event event;
    ssize_t written;
    int fd;

    /* 频率需能容纳在 input_event.value(int) 内 */
    if (frequency_hz > INT_MAX) {
        errno = EINVAL;
        return -1;
    }

    /* 打开蜂鸣器设备 */
    fd = open(BUZZER_DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("open buzzer failed");
        return -1;
    }

    /* 构造声音事件：类型 EV_SND、码 SND_TONE、值 = 频率 */
    memset(&event, 0, sizeof(event));
    event.type = EV_SND;
    event.code = SND_TONE;
    event.value = (int)frequency_hz;

    /* 写入设备（被信号中断时重试） */
    do {
        written = write(fd, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);

    if (written != (ssize_t)sizeof(event)) {
        if (written >= 0) {
            errno = EIO;
        }
        perror("write buzzer failed");
        close(fd);
        return -1;
    }
    if (close(fd) < 0) {
        perror("close buzzer failed");
        return -1;
    }

    return 0;
}

/**
 * @brief 播放单个音符：以指定频率鸣响 duration_ms 后静音，
 *        并插入 30ms 间隔
 */
static int play_note(unsigned int frequency_hz, unsigned int duration_ms)
{
    if (buzzer_set(frequency_hz) < 0) {
        return -1;
    }
    delay_ms(duration_ms);
    if (buzzer_set(BUZZER_OFF_FREQUENCY) < 0) {
        return -1;
    }
    delay_ms(30);
    return 0;
}

/**
 * @brief 播放《小星星》旋律（提示音）
 * @return 成功 0；失败 -1
 */
int buzzer_play_music(void)
{
    /* 音符频率表（C5=523 起，共 42 个音符） */
    static const unsigned int frequencies[] = {
        523, 523, 784, 784, 880, 880, 784,
        698, 698, 659, 659, 587, 587, 523,
        784, 784, 698, 698, 659, 659, 587,
        784, 784, 698, 698, 659, 659, 587,
        523, 523, 784, 784, 880, 880, 784,
        698, 698, 659, 659, 587, 587, 523
    };
    /* 每个音符的时长表（ms） */
    static const unsigned int durations[] = {
        300, 300, 300, 300, 300, 300, 600,
        300, 300, 300, 300, 300, 300, 600,
        300, 300, 300, 300, 300, 300, 600,
        300, 300, 300, 300, 300, 300, 600,
        300, 300, 300, 300, 300, 300, 600,
        300, 300, 300, 300, 300, 300, 600
    };
    size_t i;

    /* 依次播放每个音符，中途失败则立即静音并返回错误 */
    for (i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++) {
        if (play_note(frequencies[i], durations[i]) < 0) {
            buzzer_set(BUZZER_OFF_FREQUENCY);
            return -1;
        }
    }
    return buzzer_set(BUZZER_OFF_FREQUENCY);
}
