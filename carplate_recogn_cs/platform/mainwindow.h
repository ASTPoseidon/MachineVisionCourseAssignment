#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "public.h"
#include "config.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define TIMER_DISPLAY_INTERV_MS			10

#define BACKGROUND_IMAGE    WORK_BASE_DIR"/resource/background.png"

#define STR_CONNECT           "连接"
#define STR_DISCONNECT        "断开"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void window_display(void);

    void on_connectBtn_clicked();

private:
    Ui::MainWindow *ui;
    QTimer  *display_timer;				// 用于刷新显示
    QImage  backgroundImg;
    unsigned char videobuf[FRAME_BUF_SIZE];      // 用于缓存视频图像
    int     video_display_flag;              // 显示视频
};


int mainwindow_init(void);

#endif // MAINWINDOW_H
