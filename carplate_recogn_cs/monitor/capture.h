#ifndef CAPTURE_H
#define CAPTURE_H

#include "config.h"
#include <linux/videodev2.h>


#define QUE_BUF_MAX_NUM		5


struct buffer_info
{
    unsigned char *addr;
    int len;
};


struct v4l2cap_info
{
    int fd;
    struct v4l2_format format;
    struct buffer_info buffer[QUE_BUF_MAX_NUM];
};


int start_capture_task(char *dev);


#endif // CAPTURE_H
