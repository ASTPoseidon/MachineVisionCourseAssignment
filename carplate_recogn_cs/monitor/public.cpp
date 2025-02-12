#include "stdio.h"
#include "string.h"
#include "public.h"
#include "config.h"

static unsigned char *newframe_buf;
static int newframe_len = 0;
static int frame_index = 0;
static pthread_mutex_t	newframe_mut;

/*  将jpge/mjpge格式转换为QImage */
QImage jpeg_to_QImage(unsigned char *data, int len)
{
    QImage qtImage;

    if(data==NULL || len<=0)
        return qtImage;

    qtImage.loadFromData(data, len);
    if(qtImage.isNull())
    {
        printf("ERROR: %s: QImage is null !\n", __FUNCTION__);
    }

    return qtImage;
}

int v4l2cap_update_newframe(unsigned char *data, int len)
{
    int flush_len = 0;

    if(len > FRAME_BUF_SIZE)
        flush_len = FRAME_BUF_SIZE;
    else
        flush_len = len;

    pthread_mutex_lock(&newframe_mut);
    memset(newframe_buf, 0, FRAME_BUF_SIZE);
    memcpy(newframe_buf, data, flush_len);
    pthread_mutex_unlock(&newframe_mut);
    newframe_len = flush_len;
    frame_index ++;

    if(frame_index <= 0)
        frame_index = 1;

    return 0;
}

/* 返回值：-1 出错，0-图像没有更新还是上一帧，>0 图像的编号（递增） */
int capture_get_newframe(unsigned char *data, int size, int *len)
{
    int tmpLen;

    if(newframe_len <= 0)
        return -1;

    tmpLen = (newframe_len <size ? newframe_len:size);
    if(tmpLen < newframe_len)
    {
        printf("Warning: %s: bufout size[%d] < frame size[%d] !!!\n", __FUNCTION__, size, newframe_len);
    }
    if(tmpLen <= 0)
    {
        //printf("Warning: %s: no data !!!\n", __FUNCTION__);
        return -1;
    }

    pthread_mutex_lock(&newframe_mut);
    memcpy(data, newframe_buf, tmpLen);
    pthread_mutex_unlock(&newframe_mut);
    *len = tmpLen;

    return frame_index;
}

int v4l2cap_clear_newframe(void)
{
    pthread_mutex_lock(&newframe_mut);
    memset(newframe_buf, 0, FRAME_BUF_SIZE);
    pthread_mutex_unlock(&newframe_mut);
    newframe_len = 0;

    return 0;
}

/* the frame memory use to sotre the newest one frame from capture or server */
int newframe_mem_init(void)
{

    pthread_mutex_init(&newframe_mut, NULL);

    newframe_buf = (unsigned char *)calloc(1, FRAME_BUF_SIZE);
    if(newframe_buf == NULL)
    {
        printf("ERROR: %s: malloc failed\n", __FUNCTION__);
        return -1;
    }

    frame_index = 0;

    return 0;
}

