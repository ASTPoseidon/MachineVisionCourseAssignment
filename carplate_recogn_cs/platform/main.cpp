#include "mainwindow.h"
#include <QApplication>
#include "opencv_carplate.h"
#include "public.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 初始化显示界面
    mainwindow_init();

    newframe_mem_init();

    start_car_recogn_task();

    return a.exec();
}
