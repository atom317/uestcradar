#include "radar_reader.hpp"

#include <data.h>
#include <sdk.h>

#include <QDebug>

RadarReader::RadarReader(QObject* parent)
    : QThread(parent), m_running(false) {}

RadarReader::~RadarReader() {
    stop();
    wait();
}

void RadarReader::stop() {
    m_running = false;
}

void RadarReader::run() {
    qInfo() << "[RadarReader] 正在接入 IQ 输入...";

    try {
        uestcradar::Input<uestcradar::IQFrame> input;
        qInfo() << "[RadarReader] IQ 输入已接通";
        m_running = true;

        while (m_running && !isInterruptionRequested()) {
            auto frame = input.read();
            if (frame.data.rows() >= 1) {
                const auto channel = frame.data[0];
                emit channel1DataReceived(QByteArray{
                    reinterpret_cast<const char*>(channel.data()),
                    static_cast<int>(channel.size_bytes())});
            }
            if (frame.data.rows() >= 2) {
                const auto channel = frame.data[1];
                emit channel2DataReceived(QByteArray{
                    reinterpret_cast<const char*>(channel.data()),
                    static_cast<int>(channel.size_bytes())});
            }
        }
    } catch (const std::exception& error) {
        qWarning() << "[RadarReader] IQ 输入失败：" << error.what();
    }
    qInfo() << "[RadarReader] 后台读取线程已退出";
}
