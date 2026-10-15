/*
 * buzzer.h —— 蜂鸣器驱动（Linux input 子系统的 EV_SND 事件）
 *
 * 【硬件原理】
 *   开发板的蜂鸣器由 input 子系统的声音设备管理，设备节点：
 *     /dev/input/by-path/platform-beeper-event
 *   向该设备写入 EV_SND / SND_TONE 事件即可控制音调：
 *     - value = 频率(Hz) → 以指定频率鸣叫；
 *     - value = 0        → 停止鸣叫（静音）。
 *   每次鸣叫前 open、写完后 close，操作无状态、可随时改变频率。
 *
 * 【本项目用途】（分级声音告警）
 *   E01 电压异常   → 低频间断鸣响（1000Hz，鸣 200ms / 静 600ms）
 *   E03~E05 高级别 → 高频持续报警（4000Hz 长鸣）
 *   KEY3           → 静音（frequency = 0）
 */

#ifndef BUZZER_H
#define BUZZER_H

/* 蜂鸣器输入设备节点（by-path 稳定路径） */
#define BUZZER_DEVICE_PATH "/dev/input/by-path/platform-beeper-event"
/* 停止鸣叫的频率值（0 表示静音） */
#define BUZZER_OFF_FREQUENCY 0U

/**
 * @brief 设置蜂鸣器音调
 * @param frequency_hz 鸣叫频率(Hz)；传 BUZZER_OFF_FREQUENCY(0) 表示停止
 * @return 成功 0；失败 -1
 */
int buzzer_set(unsigned int frequency_hz);

/**
 * @brief 播放一首提示音（《小星星》旋律，系统上线/调试用）
 * @return 成功 0；失败 -1
 */
int buzzer_play_music(void);

#endif
