#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include "socket_client.h"
#include "config.h"


static MainWindow *mainwindow;

extern char g_carplate_str[16];
extern int g_carplate_update;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /* set window title - 设置窗口标题 */
    setWindowTitle(DEFAULT_WINDOW_TITLE);

    ui->clockLab->setWordWrap(true);	// 自动换行
    ui->connectBtn->setCheckable(true);
    video_display_flag = 1;

    /* 加载并显示背景图 */
    backgroundImg.load(BACKGROUND_IMAGE);
    ui->videoLab->setPixmap(QPixmap::fromImage(backgroundImg));

    display_timer = new QTimer(this);
    connect(display_timer, SIGNAL(timeout()), this, SLOT(window_display()));
    display_timer->start(TIMER_DISPLAY_INTERV_MS);


}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::window_display(void)
{
    static int old_frame_index = 0;
    QImage videoQImage;
    QString videoStr;
    int frame_len = 0;
    int ret;

    display_timer->stop();

    // 显示时钟
    QDateTime time = QDateTime::currentDateTime();
    QString strDate = time.toString("yyyy-MM-dd hh:mm:ss dddd");
    ui->clockLab->setText(strDate);

    if(video_display_flag)
    {
        // 获取最新一帧图像
        ret = capture_get_newframe(videobuf, FRAME_BUF_SIZE, &frame_len);
        if(ret > 0 && ret != old_frame_index)
        {
            old_frame_index = ret;
            //qDebug() << "frame index " << ret;
            videoQImage = jpeg_to_QImage(videobuf, frame_len);

            // 显示一帧图像
            ui->videoLab->setPixmap(QPixmap::fromImage(videoQImage));
            ui->videoLab->show();
        }

        if(g_carplate_update)
        {
            ui->carplateLab->setText(g_carplate_str);
            g_carplate_update = 0;
        }
    }

    display_timer->start(TIMER_DISPLAY_INTERV_MS);

}

void MainWindow::on_connectBtn_clicked()
{
    if(ui->connectBtn->isChecked())
    {
        QString svr_ip;
        svr_ip = ui->serverIpEdit->text();
        qDebug() << "server ip" << svr_ip;
        start_socket_client_task(svr_ip.toLocal8Bit().data());
        ui->connectBtn->setText(STR_DISCONNECT);
    }
    else
    {
        stop_socket_client_task();
        ui->connectBtn->setText(STR_CONNECT);
        ui->videoLab->setPixmap(QPixmap::fromImage(backgroundImg));
    }
}

/* main window initial - 主界面初始化 */
int mainwindow_init(void)
{
    mainwindow = new MainWindow;

    mainwindow->show();

    return 0;
}
