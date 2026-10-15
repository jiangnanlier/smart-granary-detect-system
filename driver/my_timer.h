/*
 * my_timer.h —— 周期定时器库（基于 POSIX timer_create 封装）
 *
 * 【实现原理】
 *   底层使用 POSIX 定时器：
 *     - timer_create(CLOCK_MONOTONIC, ...)：单调递增时钟，不受系统
 *       时间被修改的影响，适合周期性任务；
 *     - sigev_notify = SIGEV_THREAD：到期时由系统新建线程调用回调，
 *       不占用信号槽、无需信号处理函数；
 *     - timer_settime()：设置首次触发时间(it_value)与周期
 *       (it_interval)，实现周期性触发。
 *
 * 【使用约束】
 *   - 最多同时存在 MY_TIMER_MAX(8) 个定时器；
 *   - 创建时即开始运行，period_ms 必须为正；
 *   - 句柄（Timer_Handle）即内部槽位下标，范围 0~7；
 *   - 回调运行在独立线程中，应尽量短小，避免在回调中做耗时 IO，
 *     与其他线程共享的数据需自行加锁保护。
 */

#ifndef MY_TIMER_H
#define MY_TIMER_H

/* 定时器最大数量 */
#define MY_TIMER_MAX 8

/* 定时器句柄（内部为定时器表下标，0 ~ MY_TIMER_MAX-1；-1 表示无效） */
typedef int Timer_Handle;
/* 定时器到期回调函数类型 */
typedef void (*Timer_Callback)(void);

/* 创建定时器并立即启动；period_ms 必须为正。
 * 成功返回句柄(>=0)，失败返回 -1 */
Timer_Handle my_timer_create(int period_ms, Timer_Callback callback);
/* 启动（恢复）已停止的定时器 */
int my_timer_start(Timer_Handle handle);
/* 停止定时器（保留句柄，可再次 start） */
int my_timer_stop(Timer_Handle handle);
/* 销毁定时器并释放句柄 */
int my_timer_destroy(Timer_Handle handle);

#endif
