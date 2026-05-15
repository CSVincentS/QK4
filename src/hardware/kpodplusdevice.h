#ifndef KPODPLUSDEVICE_H
#define KPODPLUSDEVICE_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <atomic>

// Forward declarations for libusb
struct libusb_context;
struct libusb_device_handle;

#ifdef Q_OS_LINUX
class KpodUdevWorker;
#endif

struct KpodPlusDeviceInfo {
    bool detected = false;
    QString productName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString firmwareVersion;
    QString deviceId;
    quint8 busNumber = 0;
    quint8 deviceAddress = 0;
};

// Worker that blocks on EP02 interrupt transfers to read keyer output (KZ/KX strings).
// Runs on a dedicated thread; emits keyerDataReceived() with raw data.
class KpodPlusEp02Worker : public QObject {
    Q_OBJECT

public:
    explicit KpodPlusEp02Worker(QObject *parent = nullptr);

    void setDeviceHandle(libusb_device_handle *handle);

    // Thread-safe stop flag
    void requestStop();

public slots:
    void run();

signals:
    void keyerDataReceived(const QByteArray &data);

private:
    libusb_device_handle *m_handle = nullptr;
    std::atomic<bool> m_running{false};
};

class KpodPlusDevice : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x04D8;
    static const quint16 PRODUCT_ID_KPOD = 0xF12D;
    static const quint16 PRODUCT_ID_ELECRAFT = 0xEFA5;
    static const quint8 VENDOR_INTERFACE_CLASS = 255;

    // Rocker switch positions (same encoding as KPOD)
    enum RockerPosition { RockerCenter = 0, RockerRight = 1, RockerLeft = 2 };
    Q_ENUM(RockerPosition)

    explicit KpodPlusDevice(QObject *parent = nullptr);
    ~KpodPlusDevice();

    // Detection
    bool isDetected() const;
    KpodPlusDeviceInfo deviceInfo() const;
    static KpodPlusDeviceInfo detectDevice();

    // Polling control (EP01 encoder/buttons + EP02 keyer reader)
    bool startPolling();
    void stopPolling();
    bool isPolling() const;

    // Current state
    RockerPosition rockerPosition() const;

    // Keyer configuration commands (sent via EP01)
    void setKeyerSpeed(int wpm);
    void setCwPitch(int freqHz);
    void setKeyerParams(int iambicMode, bool paddleReversed);
    void setEncodeMode(int mode);
    void setStuckTimeout(int seconds);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady();
    void encoderRotated(int ticks);
    void rockerPositionChanged(RockerPosition position);
    void buttonTapped(int buttonNumber);
    void buttonHeld(int buttonNumber);
    void keyerDataReceived(const QByteArray &data);
    void pollError(const QString &error);

private slots:
    void initialize();
    void poll();
    void onDeviceArrived();
    void onDeviceRemoved();

private:
    bool openDevice();
    void closeDevice();
    void processResponse(const unsigned char *buffer);
    bool sendCommand(const unsigned char *data, int length);
    void startEp02Reader();
    void stopEp02Reader();

    // Hotplug monitoring
    void setupHotplugMonitoring();
    void teardownHotplugMonitoring();

    KpodPlusDeviceInfo m_deviceInfo;
    libusb_context *m_usbContext = nullptr;
    libusb_device_handle *m_usbHandle = nullptr;
    bool m_interfaceClaimed = false;

    QTimer *m_pollTimer = nullptr;
    static const int POLL_INTERVAL_MS = 20;
    RockerPosition m_lastRockerPosition = RockerCenter;
    quint8 m_lastButtonState = 0;
    bool m_holdEmitted = false;

    // EP02 reader thread
    KpodPlusEp02Worker *m_ep02Worker = nullptr;
    QThread *m_ep02Thread = nullptr;

    // Hotplug monitoring
#ifdef Q_OS_LINUX
    KpodUdevWorker *m_udevWorker = nullptr;
    QThread *m_udevThread = nullptr;
#else
    QTimer *m_presenceTimer = nullptr;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;
    void checkDevicePresence();
#endif
};

#endif // KPODPLUSDEVICE_H
