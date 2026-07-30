#include <QCoreApplication>
#include <QDebug>
#include <QObject>
#include <csignal>

#include "radar_reader.hpp"
#include "channel_processor.hpp"

namespace {
    QCoreApplication* appInstance = nullptr;
    void handleSignal(int) {
        if (appInstance) {
            qInfo() << "Stop signal received, shutting down gracefully...";
            appInstance->quit();
        }
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    appInstance = &app;
    
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    qInfo() << "启动 Qt5 多通道并行雷达算法基座 (版本: " << qVersion() << ")";

    // 1. 启动雷达数据源采集 (自动在后台无阻塞循环读取 SDK，并进行通道解包)
    RadarReader reader;

    // 2. 初始化多通道并行处理器 (各自运行在独立的 QThread)
    ChannelProcessor channel1("Channel_1_FFT");
    ChannelProcessor channel2("Channel_2_CFAR");

    // 3. 将雷达数据分发给不同的通道进行异步处理 (跨线程信号槽)
    QObject::connect(&reader, &RadarReader::channel1DataReceived, 
                     &channel1, &ChannelProcessor::processData, Qt::QueuedConnection);
    QObject::connect(&reader, &RadarReader::channel2DataReceived, 
                     &channel2, &ChannelProcessor::processData, Qt::QueuedConnection);

    // 4. 开始运行
    reader.start();
    int ret = app.exec();

    // 退出清理
    reader.stop();
    reader.wait();
    
    return ret;
}
