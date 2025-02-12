#ifndef PUBLIC_H
#define PUBLIC_H

#include <QImage>

#define UNUSED_1(a)         {(void)a;}
#define UNUSED_2(a,b)       {(void)a;(void)b;}
#define UNUSED_3(a,b,c)     {(void)a;(void)b;(void)c;}
#define UNUSED_4(a,b,c,d)   {(void)a;(void)b;(void)c;(void)d;}
#define UNUSED_5(a,b,c,d,e) {(void)a;(void)b;(void)c;(void)d;(void)e;}

#define WORK_BASE_DIR        "../monitor/"


QImage jpeg_to_QImage(unsigned char *data, int len);


/* 返回值：-1 出错，0-图像没有更新还是上一帧，>0 图像的编号（递增） */
int capture_get_newframe(unsigned char *data, int size, int *len);

int v4l2cap_update_newframe(unsigned char *data, int len);

int v4l2cap_clear_newframe(void);

int newframe_mem_init(void);


#endif // PUBLIC_H
