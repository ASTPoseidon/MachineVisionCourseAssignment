#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include "socket_server.h"
#include "capture.h"
#include "public.h"
#include "config.h"

int g_send_video_flag = 1;
static char *capture_video_dev = NULL;

struct v4l2cap_info capture_info;
extern server_info_t g_server_info;


int capture_init(struct v4l2cap_info *capture)
{
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_format format;
    struct v4l2_requestbuffers reqbuf_param;
    struct v4l2_buffer buffer[QUE_BUF_MAX_NUM];
    unsigned int v4l2_fmt[2] = {V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_JPEG};
    int i, ret;

    memset(capture, 0, sizeof(struct v4l2cap_info));

    capture->fd = open(capture_video_dev, O_RDWR);	// 打开摄像头
    if(capture->fd < 0)
    {
        printf("ERROR: open video dev [%s] failed !\n", capture_video_dev);
        ret = -1;
        goto ERR_1;
    }
    printf("open video dev [%s] successfully .\n", capture_video_dev);

    /* get supported format */
    memset(&fmtdesc, 0, sizeof(struct v4l2_fmtdesc));
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    do{
        ret = ioctl(capture->fd, VIDIOC_ENUM_FMT, &fmtdesc);
        if(ret == 0)
        {
            printf("[ret:%d]video description: %s\n", ret, fmtdesc.description);
            fmtdesc.index ++;
        }
    }while(ret == 0);

    /* try the capture format */
    for(i=0; i<(int)(sizeof(v4l2_fmt)/sizeof(int)); i++)
    {
        /* configure video format */
        memset(&format, 0, sizeof(struct v4l2_format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = DEFAULT_CAPTURE_WIDTH;
        format.fmt.pix.height = DEFAULT_CAPTURE_HEIGH;
        format.fmt.pix.pixelformat = v4l2_fmt[i];
        format.fmt.pix.field = V4L2_FIELD_INTERLACED;
        ret = ioctl(capture->fd, VIDIOC_S_FMT, &format);
        if(ret < 0)
        {
            ret = -2;
            goto ERR_2;
        }

        printf("[try %d] set v4l2 format: ", i);

        /* get video format */
        capture->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl(capture->fd, VIDIOC_G_FMT, &capture->format);
        if(ret < 0)
        {
            printf("ERROR: get video format failed[ret:%d] !\n", ret);
            ret = -3;
            goto ERR_3;
        }
        switch(v4l2_fmt[i])
        {
            case V4L2_PIX_FMT_JPEG: printf("V4L2_PIX_FMT_JPEG \n");
                break;
            case V4L2_PIX_FMT_YUYV: printf("V4L2_PIX_FMT_YUYV \n");
                break;
            case V4L2_PIX_FMT_MJPEG: printf("V4L2_PIX_FMT_MJPEG \n");
                break;

            default:
                printf("ERROR: value is illegal !\n");
        }

        if(capture->format.fmt.pix.pixelformat == v4l2_fmt[i])
        {
            printf("try successfully.\n");
            printf("video width * height = %d * %d\n", capture->format.fmt.pix.width, capture->format.fmt.pix.height);
            break;
        }
    }
    if(i >= (int)(sizeof(v4l2_fmt)/sizeof(int)))
    {
        printf("ERROR: Not support capture foramt !!!\n");
        ret = -4;
        goto ERR_4;
    }

	// 申请缓存
    memset(&reqbuf_param, 0, sizeof(struct v4l2_requestbuffers));
    reqbuf_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf_param.memory = V4L2_MEMORY_MMAP;
    reqbuf_param.count = QUE_BUF_MAX_NUM;
    ret = ioctl(capture->fd, VIDIOC_REQBUFS, &reqbuf_param);
    if(ret < 0)
    {
        ret = -6;
        goto ERR_6;
    }

    /* 设置视频缓存队列 */
    for(i=0; i<QUE_BUF_MAX_NUM; i++)
    {
        memset(&buffer[i], 0, sizeof(struct v4l2_buffer));
        buffer[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer[i].memory = V4L2_MEMORY_MMAP;
        buffer[i].index = i;
        ret = ioctl(capture->fd, VIDIOC_QUERYBUF, &buffer[i]);
        if(ret < 0)
        {
            ret = -7;
            goto ERR_7;
        }

        capture->buffer[i].len = buffer[i].length;
        capture->buffer[i].addr = (unsigned char *)mmap(NULL, buffer[i].length, PROT_READ | PROT_WRITE, \
                                    MAP_SHARED, capture->fd, buffer[i].m.offset);
        printf("buffer[%d]: addr = %p, len = %d\n", i, capture->buffer[i].addr, capture->buffer[i].len);

        ret = ioctl(capture->fd, VIDIOC_QBUF, &buffer[i]);
        if(ret < 0)
        {
            ret = -8;
            goto ERR_8;
        }
    }

    return 0;

    ERR_8:
    ERR_7:
        for(; i>=0; i--)
        {
            if(capture->buffer[i].addr != NULL)
                munmap(capture->buffer[i].addr, capture->buffer[i].len);
        }
    ERR_6:
    ERR_4:
    ERR_3:
    ERR_2:
        close(capture->fd);

    ERR_1:

    return ret;
}

void capture_deinit(struct v4l2cap_info *capture)
{
    int i;

    for(i=0; i<QUE_BUF_MAX_NUM; i++)
    {
        munmap(capture->buffer[i].addr, capture->buffer[i].len);
    }

    close(capture->fd);
}

int v4l2cap_start(struct v4l2cap_info *capture)
{
    enum v4l2_buf_type type;
    int ret;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(capture->fd, VIDIOC_STREAMON, &type);
    if(ret < 0)
        return -1;

    return 0;
}

void v4l2cap_stop(struct v4l2cap_info *capture)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(capture->fd, VIDIOC_STREAMOFF, &type);
}


void *capture_thread(void *arg)
{
    struct v4l2cap_info *capture = &capture_info;
    struct v4l2_buffer v4l2buf;
    static unsigned int index = 0;
    int frame_len;
    int ret;

    (void)arg;
    (void)frame_len;

    ret = capture_init(capture);
    if(ret != 0)
    {
        printf("ERROR: capture init failed, ret: %d\n", ret);
        return NULL;
    }
    printf("capture init successfully .\n");

    v4l2cap_start(capture);

    while(1)
    {
        memset(&v4l2buf, 0, sizeof(struct v4l2_buffer));
        v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2buf.memory = V4L2_MEMORY_MMAP;
        v4l2buf.index = index % QUE_BUF_MAX_NUM;

        /* get v4l2 frame data */
        ret = ioctl(capture->fd, VIDIOC_DQBUF, &v4l2buf);
        if(ret < 0)
        {
            printf("ERROR: get VIDIOC_DQBUF failed, ret: %d\n", ret);
            continue;
        }

        v4l2cap_update_newframe(capture->buffer[v4l2buf.index].addr, capture->buffer[v4l2buf.index].len);

        if(g_send_video_flag)
        {
            //发送一帧图像
            proto_0x21_sendframe(0, capture->buffer[v4l2buf.index].addr, capture->buffer[v4l2buf.index].len);
        }

        ret = ioctl(capture->fd, VIDIOC_QBUF, &v4l2buf);
        if(ret < 0)
        {
            printf("ERROR: get VIDIOC_QBUF failed, ret: %d\n", ret);
            continue;
        }

        index ++;
    }

    v4l2cap_stop(capture);

    capture_deinit(capture);
    printf("%s: exit --\n", __FUNCTION__);
    return NULL;
}

int start_capture_task(char *dev)
{
    pthread_t tid;
    int ret;

    if(dev)
        capture_video_dev = dev;
    else
        capture_video_dev = (char *)DEFAULT_CAPTURE_DEV;

    ret = pthread_create(&tid, NULL, capture_thread, NULL);
    if(ret != 0)
    {
        return -1;
    }

    return 0;
}

