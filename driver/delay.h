/*
 * delay.h —— 延时库（基于 nanosleep 封装）
 *
 * 【实现原理】
 *   底层使用 POSIX nanosleep()，支持纳秒级精度，
 *   远优于早期的 usleep / sleep 系列函数。
 *   封装时统一处理 EINTR（被信号中断）情况：自动补睡剩余时间，
 *   保证实际延时时间不缩水。
 *
 * 【使用场景】蜂鸣器音符时长、告警节奏控制、主循环节拍等。
 */

#ifndef DELAY_H
#define DELAY_H

/**
 * @brief 毫秒级延时
 * @param milliseconds 延时毫秒数
 */
void delay_ms(unsigned int milliseconds);

/**
 * @brief 微秒级延时
 * @param microseconds 延时微秒数
 */
void delay_us(unsigned int microseconds);

#endif
