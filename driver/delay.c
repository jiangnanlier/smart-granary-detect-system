/*
 * delay.c —— 延时库实现（nanosleep 封装）
 *
 * 实现要点：
 *   1. delay_time() 将“秒 + 纳秒”交给 nanosleep；
 *   2. nanosleep 被信号中断（返回 -1 且 errno == EINTR）时，
 *      剩余时间通过第二个参数 remaining 回传，循环补睡，
 *      保证总延时时间不缩水；
 *   3. delay_ms / delay_us 分别将毫秒 / 微秒拆分为
 *      “秒 + 纳秒”两部分后调用 delay_time。
 */

#define _POSIX_C_SOURCE 199309L

#include "delay.h"

#include <errno.h>
#include <time.h>

/**
 * @brief 按“秒 + 纳秒”延时，被信号中断时自动补睡
 */
static void delay_time(time_t seconds, long nanoseconds)
{
    struct timespec remaining;

    remaining.tv_sec = seconds;
    remaining.tv_nsec = nanoseconds;
    while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR) {
    }
}

/**
 * @brief 毫秒级延时
 */
void delay_ms(unsigned int milliseconds)
{
    delay_time((time_t)(milliseconds / 1000U),
               (long)(milliseconds % 1000U) * 1000000L);
}

/**
 * @brief 微秒级延时
 */
void delay_us(unsigned int microseconds)
{
    delay_time((time_t)(microseconds / 1000000U),
               (long)(microseconds % 1000000U) * 1000L);
}
