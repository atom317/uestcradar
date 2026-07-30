#include "channel_processor.hpp"
#include <QDebug>
#include <QThread>

ChannelProcessor::ChannelProcessor(const QString& name, QObject* parent)
    : QObject(parent), m_name(name) 
{
    // 实例化一个专属的后台线程
    m_workerThread = new QThread();
    
    // 把自己移动到这个后台线程中，此后所有收到的 slot 调用，只要是 QueuedConnection，
    // 都会自动派发到该线程的事件循环中执行，不会阻塞 main 线程或 reader 线程。
    this->moveToThread(m_workerThread);
    
    // 启动后台线程的事件循环
    m_workerThread->start();
    
    qInfo().noquote() << QString("[%1] 通道处理器已在后台线程初始化完成。").arg(m_name);
}

ChannelProcessor::~ChannelProcessor() {
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
    }
}

void ChannelProcessor::processData(const QByteArray& data) {
    // 打印当前线程信息和数据长度，证明确实是在并行处理
    qInfo().noquote() << QString("[%1] 正在处理数据块，大小: %2 字节, 所在线程: 0x%3")
                         .arg(m_name)
                         .arg(data.size())
                         .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
                         
    // 模拟一段耗时的数学运算 (如 FFT / CFAR)
    QThread::msleep(100);
}
