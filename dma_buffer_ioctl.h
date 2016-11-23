#ifndef MYMISCDEV_IOCTL_H
#define MYMISCDEV_IOCTL_H

#include <linux/ioctl.h>

#define IOCTL_MAGIC_NUMBER 0xA5

struct dma_buffer_ioctl
{
    char *buffer1;
    size_t len1;
    char *buffer2;
    size_t len2;
};

struct dma_buffer_status_ioctl
{
    int filled;
};

#define IOCTL_CMD_ALLOC_BUFFERS \
    _IOR(IOCTL_MAGIC_NUMBER, 1, struct dma_buffer_ioctl)

#define IOCTL_CMD_READ_BUFFER_STATUS \
    _IOR(IOCTL_MAGIC_NUMBER, 2, struct dma_buffer_status_ioctl)


#endif

