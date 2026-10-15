/*
 * key.h —— 独立按键驱动（Linux input 输入子系统）
 *
 * 【硬件原理】
 *   FS-MP1A 的独立按键（KEY1/KEY2/KEY3）由 Linux 输入子系统管理，
 *   用户空间通过 /dev/input/eventN 字符设备读取按键事件：
 *     - 优先打开稳定的 by-path 软链接
 *       "/dev/input/by-path/platform-keys-event"；
 *     - 若不存在，则依次扫描 event0~event7，通过 ioctl(EVIOCGBIT)
 *       查询设备是否支持 EV_KEY 事件以及常用按键码，自动定位按键设备。
 *
 * 【事件模型】（struct input_event，type = EV_KEY）
 *   value = 0 → 抬起（RELEASE）
 *   value = 1 → 按下（PRESS）
 *   value = 2 → 长按自动重复（HOLD，通常应忽略以避免重复触发）
 *
 * 【使用方式】
 *   1. key_init_auto() 自动打开按键设备；
 *   2. 主循环中反复调用 key_read_event() 非阻塞读取事件（返回 0 表示无事件）；
 *   3. 退出时 key_close() 关闭设备。
 */

#ifndef KEY_H
#define KEY_H

#include <stdint.h>

/* 单次读取的最大缓存事件数 */
#define KEY_MAX_EVENTS 32
/* 稳定的按键设备软链接路径（优先使用） */
#define KEY_STABLE_DEVICE_PATH "/dev/input/by-path/platform-keys-event"
/* 自动扫描时的设备路径格式 */
#define KEY_DEVICE_PATH_FORMAT "/dev/input/event%d"
/* 自动扫描的设备数量 */
#define KEY_DEVICE_SCAN_COUNT 8
/* 设备路径缓冲区大小 */
#define KEY_DEVICE_PATH_SIZE 64

/* 按键事件类型（对应 input_event.value） */
typedef enum {
    KEY_EVENT_PRESS = 0,    /* 按下   */
    KEY_EVENT_RELEASE = 1,  /* 抬起   */
    KEY_EVENT_HOLD = 2,     /* 长按重复 */
} key_event_type_t;

/* 按键事件结构（从内核 input_event 转换而来） */
typedef struct {
    uint16_t code;             /* 按键码，如 KEY_1 / KEY_2 / KEY_ENTER */
    key_event_type_t type;     /* 事件类型 */
    uint32_t value;            /* 原始 value（保留） */
} key_event_t;

/* 按键设备句柄 */
typedef struct {
    int fd;                              /* 设备文件描述符 */
    char device_path[KEY_DEVICE_PATH_SIZE]; /* 实际打开的设备路径 */
    int initialized;                     /* 是否已初始化 */
} key_device_t;

/**
 * @brief 自动定位并打开按键设备
 *        优先尝试 by-path 稳定路径，失败则扫描 event0~event7
 * @return 成功 0；失败 -1
 */
int key_init_auto(key_device_t *device);

/**
 * @brief 打开指定的按键输入设备
 * @param device     设备句柄
 * @param device_path 设备路径（如 /dev/input/event0）
 * @return 成功 0；失败 -1
 */
int key_init(key_device_t *device, const char *device_path);

/**
 * @brief 非阻塞读取一个按键事件
 * @return 1=读到有效按键事件；0=当前无事件；-1=出错
 */
int key_read_event(key_device_t *device, key_event_t *event);

/**
 * @brief 查询某个按键当前是否处于按下状态
 * @return 1=按下中；0=未按下；-1=出错
 */
int key_is_pressed(key_device_t *device, uint16_t key_code);

/**
 * @brief 关闭按键设备
 * @return 成功 0；失败 -1
 */
int key_close(key_device_t *device);

/**
 * @brief 按键码 → 可读名称（用于日志打印）
 * @return 名称字符串；未知按键返回 "UNKNOWN"
 */
const char *key_get_name(uint16_t key_code);

#endif
