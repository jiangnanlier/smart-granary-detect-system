/*
 * key.c —— 独立按键驱动实现（Linux input 输入子系统）
 *
 * 实现要点：
 *   1. is_key_device()：打开候选设备，通过 ioctl(EVIOCGBIT) 查询其
 *      事件能力位图，确认支持 EV_KEY 且至少具备一个常用按键码，
 *      用于在多个 event 设备中自动识别出按键设备；
 *   2. key_init_auto()：优先使用 by-path 稳定软链接，其次扫描
 *      event0~event7；
 *   3. key_read_event()：以 O_NONBLOCK 非阻塞方式读取，
 *      EAGAIN 表示当前无事件（返回 0），并将内核 input_event
 *      转换为上层 key_event_t；
 *   4. key_is_pressed()：通过 ioctl(EVIOCGKEY) 查询按键实时状态。
 */

#include "key.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* 计算容纳 max_bit 个比特所需的 unsigned long 字数 */
#define BIT_WORD_COUNT(max_bit) \
    ((max_bit) / (sizeof(unsigned long) * CHAR_BIT) + 1)

/**
 * @brief 判断位图中第 bit 位是否为 1
 */
static int bit_is_set(const unsigned long *bits, unsigned int bit)
{
    const unsigned int bits_per_word = sizeof(unsigned long) * CHAR_BIT;

    return (bits[bit / bits_per_word] &
            (1UL << (bit % bits_per_word))) != 0;
}

/**
 * @brief 判断是否为常用按键码（用于识别按键设备）
 */
static int is_common_key_code(int code)
{
    return (code >= KEY_ESC && code <= KEY_KPDOT) ||
           (code >= KEY_LEFT && code <= KEY_DOWN) ||
           (code >= KEY_ENTER && code <= KEY_KPCOMMA) ||
           code == KEY_POWER || code == KEY_HOME ||
           code == KEY_MENU || code == KEY_BACK;
}

/**
 * @brief 探测指定设备是否为按键输入设备
 * @return 1=是按键设备；0=不是 / 打开失败
 */
static int is_key_device(const char *device_path)
{
    unsigned long event_bits[BIT_WORD_COUNT(EV_MAX)] = {0};
    unsigned long key_bits[BIT_WORD_COUNT(KEY_MAX)] = {0};
    int fd;
    int code;
    int found_key = 0;

    fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return 0;
    }
    /* 查询设备支持的事件类型，必须包含 EV_KEY */
    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !bit_is_set(event_bits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        close(fd);
        return 0;
    }

    /* 查询按键码位图，至少具备一个常用按键码才认定是按键设备 */
    for (code = 0; code <= KEY_MAX; code++) {
        if (is_common_key_code(code) &&
            bit_is_set(key_bits, (unsigned int)code)) {
            found_key = 1;
            break;
        }
    }

    close(fd);
    return found_key;
}

/**
 * @brief 打开指定的按键输入设备（非阻塞模式）
 * @return 成功 0；失败 -1
 */
int key_init(key_device_t *device, const char *device_path)
{
    size_t path_length;
    int fd;

    if (device == NULL || device_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    path_length = strlen(device_path);
    if (path_length >= sizeof(device->device_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open key device failed");
        return -1;
    }

    device->fd = fd;
    memcpy(device->device_path, device_path, path_length + 1);
    device->initialized = 1;
    return 0;
}

/**
 * @brief 自动定位并打开按键设备
 *        优先 by-path 稳定路径；否则扫描 event0~event7 逐个探测
 * @return 成功 0；失败 -1
 */
int key_init_auto(key_device_t *device)
{
    char device_path[KEY_DEVICE_PATH_SIZE];
    int i;
    int length;

    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* 1. 优先使用稳定的 by-path 软链接 */
    if (access(KEY_STABLE_DEVICE_PATH, R_OK) == 0) {
        return key_init(device, KEY_STABLE_DEVICE_PATH);
    }

    /* 2. 扫描 event0~event7，识别真正的按键设备 */
    for (i = 0; i < KEY_DEVICE_SCAN_COUNT; i++) {
        length = snprintf(device_path, sizeof(device_path),
                          KEY_DEVICE_PATH_FORMAT, i);
        if (length < 0 || (size_t)length >= sizeof(device_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (is_key_device(device_path)) {
            return key_init(device, device_path);
        }
    }

    errno = ENODEV;
    perror("key device not found");
    return -1;
}

/**
 * @brief 非阻塞读取一个按键事件
 * @return 1=读到有效按键事件；0=当前无事件；-1=出错
 */
int key_read_event(key_device_t *device, key_event_t *event)
{
    struct input_event input;
    ssize_t bytes_read;

    if (device == NULL || event == NULL || !device->initialized) {
        errno = EINVAL;
        return -1;
    }

    /* 非阻塞读取，被信号中断时重试 */
    do {
        bytes_read = read(device->fd, &input, sizeof(input));
    } while (bytes_read < 0 && errno == EINTR);

    /* EAGAIN / EWOULDBLOCK：当前没有待处理事件，属正常情况 */
    if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    if (bytes_read < 0) {
        perror("read key event failed");
        return -1;
    }
    if (bytes_read != (ssize_t)sizeof(input)) {
        errno = EIO;
        return -1;
    }
    /* 非按键类事件（如同步事件）直接跳过 */
    if (input.type != EV_KEY) {
        return 0;
    }

    /* 转换为上层事件结构 */
    event->code = input.code;
    event->value = (uint32_t)input.value;
    if (input.value == 0) {
        event->type = KEY_EVENT_RELEASE;
    } else if (input.value == 1) {
        event->type = KEY_EVENT_PRESS;
    } else if (input.value == 2) {
        event->type = KEY_EVENT_HOLD;
    } else {
        errno = EPROTO;
        return -1;
    }

    return 1;
}

/**
 * @brief 查询某个按键当前是否处于按下状态
 * @return 1=按下中；0=未按下；-1=出错
 */
int key_is_pressed(key_device_t *device, uint16_t key_code)
{
    unsigned long key_bits[BIT_WORD_COUNT(KEY_MAX)] = {0};

    if (device == NULL || !device->initialized || key_code > KEY_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (ioctl(device->fd, EVIOCGKEY(sizeof(key_bits)), key_bits) < 0) {
        perror("query key state failed");
        return -1;
    }
    return bit_is_set(key_bits, key_code);
}

/**
 * @brief 关闭按键设备并复位句柄
 * @return 成功 0；失败 -1
 */
int key_close(key_device_t *device)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!device->initialized) {
        return 0;
    }
    if (close(device->fd) < 0) {
        perror("close key device failed");
        return -1;
    }

    device->fd = -1;
    device->device_path[0] = '\0';
    device->initialized = 0;
    return 0;
}

/**
 * @brief 按键码 → 可读名称（日志打印用）
 * @return 名称字符串；未知按键返回 "UNKNOWN"
 */
const char *key_get_name(uint16_t key_code)
{
    switch (key_code) {
        case KEY_ESC: return "ESC";
        case KEY_1: return "1";
        case KEY_2: return "2";
        case KEY_3: return "3";
        case KEY_4: return "4";
        case KEY_5: return "5";
        case KEY_6: return "6";
        case KEY_7: return "7";
        case KEY_8: return "8";
        case KEY_9: return "9";
        case KEY_0: return "0";
        case KEY_ENTER: return "ENTER";
        case KEY_LEFT: return "LEFT";
        case KEY_RIGHT: return "RIGHT";
        case KEY_UP: return "UP";
        case KEY_DOWN: return "DOWN";
        case KEY_SPACE: return "SPACE";
        case KEY_A: return "A";
        case KEY_B: return "B";
        case KEY_C: return "C";
        case KEY_D: return "D";
        case KEY_POWER: return "POWER";
        case KEY_HOME: return "HOME";
        case KEY_MENU: return "MENU";
        case KEY_BACK: return "BACK";
        case KEY_VOLUMEUP: return "VOLUME_UP";
        case KEY_VOLUMEDOWN: return "VOLUME_DOWN";
        default: return "UNKNOWN";
    }
}
