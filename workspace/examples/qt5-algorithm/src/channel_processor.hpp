#pragma once

#include <QObject>
#include <QByteArray>
#include <QThread>
#include <QString>

class ChannelProcessor : public QObject {
    Q_OBJECT

public:
    explicit ChannelProcessor(const QString& name, QObject* parent = nullptr);
    ~ChannelProcessor() override;

public slots:
    // 异步处理雷达数据
    void processData(const QByteArray& data);

private:
    QString m_name;
    QThread* m_workerThread;
};
