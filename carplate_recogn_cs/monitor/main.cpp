#include "mainwindow.h"
#include <QApplication>
#include <unistd.h>
#include "capture.h"
#include "socket_server.h"

int main(int argc, char *argv[])
{
    char *video_dev = NULL;
    QApplication a(argc, argv);

    if(argc > 1)
    {
        video_dev = argv[1];
        printf("[input] video: %s\n", video_dev);
    }

    newframe_mem_init();

    // 初始化显示界面
    mainwindow_init();

    sleep(1);	// only to show background image
    start_capture_task(video_dev);

    start_socket_server_task();

    return a.exec();
}
