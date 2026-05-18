#ifndef KPODDEVICE_H
#define KPODDEVICE_H

#include <QObject>
#include <QString>

class QThread;
class KpodHidWorker;

struct KpodDeviceInfo {
    bool detected = false;
    QString productName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString devicePath;
    QString firmwareVersion;
    QString deviceId;
};

// Thin Qt façade over the hidapi worker. Mirrors the KpodPlusDevice/KpodPlusUsbWorker
// split: all hid_* I/O lives on the worker thread, the façade owns the worker thread,
// re-emits worker signals, and dispatches setters via Qt::QueuedConnection. The main
// thread never touches hidapi.
class KpodDevice : public QObject {
    Q_OBJECT

public:
    enum RockerPosition {
        RockerCenter = 0, // 0b00 — VFO B / center
        RockerRight = 1,  // 0b01 — RIT / XIT
        RockerLeft = 2    // 0b10 — VFO A
    };
    Q_ENUM(RockerPosition)

    explicit KpodDevice(QObject *parent = nullptr);
    ~KpodDevice() override;

    // Cached views — safe to call from the main thread.
    bool isDetected() const;
    KpodDeviceInfo deviceInfo() const;
    bool isPolling() const;
    RockerPosition rockerPosition() const;

    // Forwarded as queued invocations to the worker thread.
    bool startPolling();
    void stopPolling();

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady();
    void encoderRotated(int ticks);
    void rockerPositionChanged(RockerPosition position);
    void buttonTapped(int buttonNumber);
    void buttonHeld(int buttonNumber);
    void pollError(const QString &error);

private:
    KpodDeviceInfo m_info;
    RockerPosition m_lastRocker = RockerCenter;
    bool m_polling = false;

    KpodHidWorker *m_hidWorker = nullptr;
    QThread *m_hidThread = nullptr;
};

#endif // KPODDEVICE_H
