#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dma_buffer_ioctl.h"

void sigint_handler(int sig)
{
}

int main()
{
    int fd = open("/dev/dma_driver", O_RDWR);
    if (fd==-1) {
        perror("open");
        return -1;
    }

    // -- Create two user-space buffers and pass it to kernel to be mapped
    size_t bufsize = 32768;
    int rc;
    void *buffer1 = 0;
    void *buffer2 = 0;
    rc = posix_memalign(&buffer1, 128, bufsize);
    rc = posix_memalign(&buffer2, 128, bufsize);
    assert(buffer1!=0 && buffer2!=0);
    printf("%p %zu\n", buffer1, bufsize);
    printf("%p %zu\n", buffer2, bufsize);
    struct dma_buffer_ioctl param = { buffer1, bufsize, buffer2, bufsize };
    rc = ioctl(fd, IOCTL_CMD_ALLOC_BUFFERS, &param);
    if (rc==-1) perror("ioctl");

    // Intention: After setting up the get_user_pages and filling up the driver_params struct with the pointers
    // to the buffers, DO NOT map to hardware nor write to registers to trigger the DMA.
    // NOT DONE: The driver will check internally for the dma data ready, trigger the DMA, unmap, check for another dma
    // data ready, trigger DMA again, unmap, and then set the bufStatus.filled to 1.

    // The intention is to read this status via ioctl saying if all the buffers are filled.
    // If all the buffers are filled we know it is safe to read the data.
    struct dma_buffer_status_ioctl bufStatus = { 0 };
    rc = ioctl(fd, IOCTL_CMD_READ_BUFFER_STATUS, &bufStatus);
    if (rc==-1) perror("ioctl");
    printf("Read buffer status %d", bufStatus.filled);

    // -- Cleanup
    free(buffer1);
    free(buffer2);
    close(fd);

    return 0;
}

