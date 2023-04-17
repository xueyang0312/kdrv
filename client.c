#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEV_KFIFO_NAME "/dev/kdrv"

int main()
{
    int fd;
    int ret;

    char message[80] = "Testing the virtual FIFO device";
    char *read_buf;

    size_t len = sizeof(message);

    fd = open(DEV_KFIFO_NAME, O_RDWR | O_NONBLOCK);

    if (fd < 0) {
        printf("open device %s failed\r\n", DEV_KFIFO_NAME);
        fprintf(stderr, "fopen() failed: %s\n", strerror(errno));
        return -1;
    }

    read_buf = (char *)malloc(2 * len);
    memset(read_buf, 0, 2 * len);

    ret = read(fd, read_buf, sizeof(read_buf));
    printf("read %d bytes\r\n", ret);
    printf("read buffer=%s\r\n", read_buf);

    ret = write(fd, message, len);
    if (ret != len) {
        printf("have write %d bytes\n", ret);
    }

    ret = write(fd, message, len);
    if (ret != len) {
        printf("have write %d bytes\n", ret);
    }

    ret = read(fd, read_buf, 2 * len);
    printf("read %d bytes\r\n", ret);
    printf("read buffer=%s\r\n", read_buf);
    return 0;
}