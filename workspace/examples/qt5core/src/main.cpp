#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QTimer>

#include <csignal>

namespace {

volatile std::sig_atomic_t stopRequested = 0;

void handleSignal(int)
{
    stopRequested = 1;
}

class WorkerThread final : public QThread
{
protected:
    void run() override
    {
        quint64 heartbeat = 0;

        while (!isInterruptionRequested()) {
            ++heartbeat;
            qInfo().noquote()
                << QStringLiteral("QtCore heartbeat #%1 at %2")
                       .arg(heartbeat)
                       .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

            for (int step = 0; step < 10 && !isInterruptionRequested(); ++step) {
                QThread::msleep(100);
            }
        }

        qInfo() << "QtCore worker thread stopped";
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("qt5core-thread-example"));

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    qInfo() << "Qt version:" << qVersion();

    WorkerThread worker;

    QObject::connect(
        &application,
        &QCoreApplication::aboutToQuit,
        &worker,
        [&worker]() {
            worker.requestInterruption();
            worker.wait();
        });

    QTimer signalPoller;
    QObject::connect(
        &signalPoller,
        &QTimer::timeout,
        &application,
        [&application]() {
            if (stopRequested != 0) {
                qInfo() << "Stop signal received";
                application.quit();
            }
        });
    signalPoller.start(100);

    worker.start();
    return application.exec();
}
