#ifndef KPODUDEVWORKER_H
#define KPODUDEVWORKER_H

#ifdef Q_OS_LINUX

#include <QObject>
#include <atomic>

class KpodUdevWorker : public QObject {
    Q_OBJECT

public:
    explicit KpodUdevWorker(quint16 vendorId, quint16 productId, QObject *parent = nullptr);
    ~KpodUdevWorker() override;

    // Thread-safe; wakes the poll() loop via the self-pipe.
    void stop();

public slots:
    // Runs the udev poll loop until stop() is called. Wired to QThread::started.
    void start();

signals:
    void deviceArrived();
    void deviceRemoved();

private:
    quint16 m_vendorId;
    quint16 m_productId;
    std::atomic<bool> m_running{false};
    int m_wakePipe[2] = {-1, -1};
};

#endif // Q_OS_LINUX

#endif // KPODUDEVWORKER_H
