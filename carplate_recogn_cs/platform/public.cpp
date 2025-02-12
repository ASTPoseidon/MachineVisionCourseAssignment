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

/* 调用时要加.clone(), testMat = QImage_to_cvMat(qiamge).clone() */
cv::Mat QImage_to_cvMat(QImage qimage)
{
    cv::Mat mat;

    if(qimage.isNull())
    {
        printf("ERROR: %s: QImage is null !\n", __FUNCTION__);
        return mat;
    }

    switch(qimage.format())
    {
        case QImage::Format_ARGB32:
        case QImage::Format_RGB32:
        case QImage::Format_ARGB32_Premultiplied:
            mat = cv::Mat(qimage.height(), qimage.width(), CV_8UC4, (void*)qimage.bits(), qimage.bytesPerLine());
            break;
        case QImage::Format_RGB888:
            mat = cv::Mat(qimage.height(), qimage.width(), CV_8UC3, (void*)qimage.bits(), qimage.bytesPerLine());
            cv::cvtColor(mat, mat, CV_BGR2RGB);
            break;
        case QImage::Format_Indexed8:
            mat = cv::Mat(qimage.height(), qimage.width(), CV_8UC1, (void*)qimage.bits(), qimage.bytesPerLine());
            break;

        default:
            printf("ERROR: %s case default !\n", __FUNCTION__);
    }

    if(mat.empty())
    {
        printf("ERROR: %s: Mat is empty !\n", __FUNCTION__);
    }

    return mat;
}

QImage cvMat_to_QImage(const cv::Mat& mat)
{

    if(mat.empty())
        printf("Mat is empty.\n");

    // 8-bits unsigned, NO. OF CHANNELS = 1
    if(mat.type() == CV_8UC1)
    {
//        qDebug() << "CV_8UC1";
        QImage image(mat.cols, mat.rows, QImage::Format_Indexed8);
        // Set the color table (used to translate colour indexes to qRgb values)
        //printf("set colors\n");
        image.setColorCount(256);
        for(int i = 0; i < 256; i++)
        {
            image.setColor(i, qRgb(i, i, i));
        }
        // Copy input Mat
        uchar *pSrc = mat.data;
        for(int row = 0; row < mat.rows; row ++)
        {
            uchar *pDest = image.scanLine(row);
            memcpy(pDest, pSrc, mat.cols);
            pSrc += mat.step;
        }
        return image;
    }
    // 8-bits unsigned, NO. OF CHANNELS = 3
    else if(mat.type() == CV_8UC3)
    {
//        qDebug() << "CV_8UC3";
        // Copy input Mat
        const uchar *pSrc = (const uchar*)mat.data;
        // Create QImage with same dimensions as input Mat
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return image.rgbSwapped();
    }
    else if(mat.type() == CV_8UC4)
    {
//        qDebug() << "CV_8UC4";
        // Copy input Mat
        const uchar *pSrc = (const uchar*)mat.data;
        // Create QImage with same dimensions as input Mat
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        return image.copy();
    }
    else
    {
//        qDebug() << "ERROR: Mat could not be converted to QImage.";
        return QImage();
    }

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

