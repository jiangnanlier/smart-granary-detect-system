/*
 * file.h —— Linux sysfs 文件读写工具库
 *
 * 本项目运行于嵌入式 Linux（FS-MP1A 开发板），传感器 / LED / 风扇等外设
 * 均通过 sysfs 虚拟文件系统交互：
 *   - IIO 传感器：/sys/bus/iio/devices/iio:deviceN/ 下的 raw / offset / scale
 *   - LED：      /sys/class/leds/ledN/brightness
 *   - 风扇：     /sys/class/hwmon/hwmonN/pwm1
 *
 * 本文件将常用的“打开 - 读写 - 关闭”操作封装为三个内联（inline）函数，
 * 上层驱动只需给出文件路径即可完成数据交换。
 *
 * 返回值约定：
 *   write_file       成功 0，失败 -1（errno 已设置）
 *   read_file        成功返回读取到的字节数，失败 -1（errno 已设置）
 *   find_iio_device  成功 0，失败 -1
 */

#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将字符串写入指定文件（覆盖写）
 * @param path 文件路径，如 "/sys/class/leds/led1/brightness"
 * @param buf  待写入的字符串（必须以 '\0' 结尾）
 * @return 成功 0；失败 -1（errno 已设置）
 *
 * 说明：
 *   - 使用 O_WRONLY | O_SYNC 同步写入，确保控制命令立即送达硬件；
 *   - 内置 EINTR（被信号中断）重试与部分写入（partial write）处理，
 *     保证整段数据完整写入。
 */
static inline int write_file(const char *path, const char *buf)
{
    int fd;
    size_t remaining;
    const char *cursor;
    ssize_t written;

    if (path == NULL || buf == NULL) {
        errno = EINVAL;
        perror("write_file");
        return -1;
    }

    fd = open(path, O_WRONLY | O_SYNC);
    if (fd < 0) {
        perror("open write failed");
        return -1;
    }

    cursor = buf;
    remaining = strlen(buf);
    while (remaining > 0) {
        written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            if (written == 0) {
                errno = EIO;
            }
            perror("write failed");
            close(fd);
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }

    if (close(fd) < 0) {
        perror("close write file failed");
        return -1;
    }
    return 0;
}

/**
 * @brief 读取文件中的字符串
 * @param path 文件路径
 * @param buf  输出缓冲区
 * @param len  缓冲区大小（至少预留 1 字节存放结尾 '\0'）
 * @return 成功返回读取到的字节数；失败 -1（errno 已设置）
 *
 * 说明：读取到的内容自动以 '\0' 结尾，方便上层直接用 strtof 等解析。
 */
static inline int read_file(const char *path, char *buf, int len)
{
    int fd;
    ssize_t ret;

    if (path == NULL || buf == NULL || len <= 0) {
        errno = EINVAL;
        perror("read_file");
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open read failed");
        return -1;
    }

    do {
        ret = read(fd, buf, (size_t)len - 1);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        perror("read failed");
        close(fd);
        return -1;
    }

    buf[ret] = '\0';
    if (close(fd) < 0) {
        perror("close read file failed");
        return -1;
    }
    return ret;
}

/**
 * @brief 在 /sys/bus/iio/devices/iio:deviceN 中查找含有指定文件的 IIO 设备
 * @param marker_file 用于识别的特征文件名（该设备下必然存在的属性）
 * @param dir         输出：找到的设备目录，如 "/sys/bus/iio/devices/iio:device0"
 * @param dirlen      dir 缓冲区大小
 * @return 成功 0；失败 -1（未找到或参数非法）
 *
 * 说明：Linux IIO 子系统的设备编号不固定，无法预知某个传感器对应
 *       iio:deviceN 中的哪一个，因此用“特征属性文件”逐个探测定位。
 */
static inline int find_iio_device(const char *marker_file, char *dir, int dirlen)
{
    char path[160];
    int i;

    if (!marker_file || !dir || dirlen <= 0) {
        return -1;
    }

    for (i = 0; i < 16; i++) {
        int path_length = snprintf(path, sizeof(path),
                                   "/sys/bus/iio/devices/iio:device%d/%s",
                                   i, marker_file);
        if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
            return -1;
        }
        if (access(path, R_OK) == 0) {
            path_length = snprintf(dir, (size_t)dirlen,
                                   "/sys/bus/iio/devices/iio:device%d", i);
            if (path_length < 0 || path_length >= dirlen) {
                return -1;
            }
            return 0;
        }
    }

    return -1;
}
#ifdef __cplusplus
}

#endif

#endif
