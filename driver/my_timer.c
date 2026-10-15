/*
 * my_timer.c —— 周期定时器库实现（POSIX timer_create 封装）
 *
 * 结构说明：
 *   - timers[] 为定时器表，每个槽位保存 timer_t、周期、回调及运行标志；
 *   - timers_mutex 保护整张表（创建 / 查找 / 启停 / 销毁及回调读取）；
 *   - 句柄即槽位下标：my_timer_create 返回空闲槽下标，
 *     my_timer_destroy 清空该槽供后续复用。
 *
 * 线程模型：
 *   - SIGEV_THREAD 通知方式下，到期时由系统线程调用 timer_handler；
 *   - timer_handler 在锁内取出回调函数指针后立即放锁再执行，
 *     避免持有锁调用用户回调造成死锁。
 */

#define _POSIX_C_SOURCE 199309L

#include "my_timer.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 单个定时器的内部信息 */
typedef struct {
    timer_t timer;             /* POSIX 定时器句柄      */
    unsigned int period_ms;    /* 周期(ms)，start 时复用 */
    int created;               /* 槽位是否已创建        */
    int running;               /* 是否正在运行          */
    Timer_Callback callback;   /* 到期回调              */
} Timer_Info;

/* 定时器表（句柄即下标） */
static Timer_Info timers[MY_TIMER_MAX];
/* 保护定时器表的互斥锁 */
static pthread_mutex_t timers_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 定时器到期处理（运行在系统创建的线程中）
 *        锁内校验并取出回调指针，放锁后再执行回调
 */
static void timer_handler(union sigval value)
{
    Timer_Info *timer = value.sival_ptr;
    Timer_Callback callback = NULL;

    if (pthread_mutex_lock(&timers_mutex) != 0) {
        return;
    }
    /* 校验指针合法且定时器处于运行状态 */
    if (timer >= timers && timer < timers + MY_TIMER_MAX &&
        timer->created && timer->running) {
        callback = timer->callback;
    }
    pthread_mutex_unlock(&timers_mutex);

    /* 放锁后执行回调，避免持锁调用用户代码 */
    if (callback != NULL) {
        callback();
    }
}

/**
 * @brief 根据周期构造 itimerspec（首次触发 = 周期，之后按周期重复）
 */
static void make_timer_spec(unsigned int period_ms, struct itimerspec *spec)
{
    memset(spec, 0, sizeof(*spec));
    spec->it_value.tv_sec = (time_t)(period_ms / 1000U);
    spec->it_value.tv_nsec = (long)(period_ms % 1000U) * 1000000L;
    spec->it_interval = spec->it_value;
}

/**
 * @brief 创建并立即启动一个周期定时器
 * @param period_ms 周期(ms)，必须为正
 * @param callback  到期回调
 * @return 成功返回句柄(0~7)；失败返回 -1
 */
Timer_Handle my_timer_create(int period_ms, Timer_Callback callback)
{
    struct sigevent event;
    struct itimerspec spec;
    int slot;

    if (period_ms <= 0 || callback == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (pthread_mutex_lock(&timers_mutex) != 0) {
        return -1;
    }

    /* 1. 查找空闲槽位 */
    for (slot = 0; slot < MY_TIMER_MAX; slot++) {
        if (!timers[slot].created) {
            break;
        }
    }
    if (slot == MY_TIMER_MAX) {
        pthread_mutex_unlock(&timers_mutex);
        errno = ENOSPC;
        return -1;
    }

    /* 2. 配置 SIGEV_THREAD 通知：到期时由系统线程调用 timer_handler */
    memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_THREAD;
    event.sigev_notify_function = timer_handler;
    event.sigev_value.sival_ptr = &timers[slot];

    if (timer_create(CLOCK_MONOTONIC, &event, &timers[slot].timer) < 0) {
        perror("timer_create failed");
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }

    /* 3. 记录参数并立即启动（首次触发即一个周期后） */
    timers[slot].period_ms = (unsigned int)period_ms;
    timers[slot].callback = callback;
    timers[slot].created = 1;
    timers[slot].running = 1;
    make_timer_spec(period_ms, &spec);
    if (timer_settime(timers[slot].timer, 0, &spec, NULL) < 0) {
        /* 启动失败：回滚该槽位 */
        perror("timer_settime failed");
        timer_delete(timers[slot].timer);
        memset(&timers[slot], 0, sizeof(timers[slot]));
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }

    pthread_mutex_unlock(&timers_mutex);
    return slot;
}

/**
 * @brief 启动（恢复）一个已停止的定时器
 * @return 成功 0；失败 -1
 */
int my_timer_start(Timer_Handle handle)
{
    struct itimerspec spec;

    if (handle < 0 || handle >= MY_TIMER_MAX ||
        pthread_mutex_lock(&timers_mutex) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!timers[handle].created) {
        pthread_mutex_unlock(&timers_mutex);
        errno = EINVAL;
        return -1;
    }

    /* 重新装载周期并置运行标志 */
    make_timer_spec(timers[handle].period_ms, &spec);
    timers[handle].running = 1;
    if (timer_settime(timers[handle].timer, 0, &spec, NULL) < 0) {
        timers[handle].running = 0;
        perror("timer_settime failed");
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }

    pthread_mutex_unlock(&timers_mutex);
    return 0;
}

/**
 * @brief 停止定时器（保留句柄，可再次 my_timer_start 恢复）
 * @return 成功 0；失败 -1
 */
int my_timer_stop(Timer_Handle handle)
{
    struct itimerspec spec;

    if (handle < 0 || handle >= MY_TIMER_MAX ||
        pthread_mutex_lock(&timers_mutex) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!timers[handle].created) {
        pthread_mutex_unlock(&timers_mutex);
        errno = EINVAL;
        return -1;
    }

    /* 清零 itimerspec 即停止计时 */
    memset(&spec, 0, sizeof(spec));
    if (timer_settime(timers[handle].timer, 0, &spec, NULL) < 0) {
        perror("timer_settime failed");
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }
    timers[handle].running = 0;

    pthread_mutex_unlock(&timers_mutex);
    return 0;
}

/**
 * @brief 销毁定时器并释放句柄（槽位清空后可被再次创建复用）
 * @return 成功 0；失败 -1
 */
int my_timer_destroy(Timer_Handle handle)
{
    struct itimerspec spec;

    if (handle < 0 || handle >= MY_TIMER_MAX ||
        pthread_mutex_lock(&timers_mutex) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!timers[handle].created) {
        pthread_mutex_unlock(&timers_mutex);
        errno = EINVAL;
        return -1;
    }

    /* 先停止再删除，最后清空槽位 */
    memset(&spec, 0, sizeof(spec));
    if (timer_settime(timers[handle].timer, 0, &spec, NULL) < 0) {
        perror("timer_settime failed");
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }
    timers[handle].running = 0;
    if (timer_delete(timers[handle].timer) < 0) {
        perror("timer_delete failed");
        pthread_mutex_unlock(&timers_mutex);
        return -1;
    }
    memset(&timers[handle], 0, sizeof(timers[handle]));

    pthread_mutex_unlock(&timers_mutex);
    return 0;
}
