#include "radar_reader.hpp"
#include "sdk.h"
#include <QDebug>

RadarReader::RadarReader(QObject* parent)
    : QThread(parent), m_running(false) {
}

RadarReader::~RadarReader() {
    stop();
    wait();
}

void RadarReader::stop() {
    m_running = false;
}

void RadarReader::run() {
    qInfo() << "[RadarReader] 尝试接入底层网络数据发送端口...";
    
    if (io_open() != 0) {
        qWarning() << "[RadarReader] 接入发送网关失败！";
        return;
    }

    qInfo() << "[RadarReader] 数据源接入成功，开始抽水...";
    m_running = true;

    // 假设底层每一次出水块大小为 8192 字节 (例如 1024 个 complex<float>)
    const int CHUNK_SIZE = 8192;
    QByteArray buffer;
    buffer.resize(CHUNK_SIZE);

    while (m_running && !isInterruptionRequested()) {
        // 阻塞读取底层数据
        int32_t bytes_read = io_read(buffer.data(), buffer.size());
        
        if (bytes_read <= 0) {
            qWarning() << "[RadarReader] 读取发生错误或对端断开，停止拉取。";
            break;
        }

        // 方案 A: 本地人工解包分发
        // 假设前一半是 Channel 1, 后一半是 Channel 2
        int halfSize = bytes_read / 2;
        if (halfSize > 0) {
            // QByteArray::mid() 会创建数据的浅拷贝(如果不修改)并带有引用计数，非常高效
            QByteArray ch1Data = buffer.mid(0, halfSize);
            QByteArray ch2Data = buffer.mid(halfSize, halfSize);

            // 跨线程投递，安全又高效
            emit channel1DataReceived(ch1Data);
            emit channel2DataReceived(ch2Data);
        }
    }

    io_close();
    qInfo() << "[RadarReader] 后台抽水线程已退出。";
}
