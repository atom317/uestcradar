#pragma once

#include <QThread>
#include <QByteArray>

class RadarReader : public QThread {
    Q_OBJECT

public:
    explicit RadarReader(QObject* parent = nullptr);
    ~RadarReader() override;

    // 请求线程安全退出
    void stop();

signals:
    // 方案A：解包后的通道数据分发信号
    void channel1DataReceived(const QByteArray& data);
    void channel2DataReceived(const QByteArray& data);

protected:
    void run() override;

private:
    bool m_running;
};
