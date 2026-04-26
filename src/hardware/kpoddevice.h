#ifndef KPODDEVICE_H
#define KPODDEVICE_H

#include <QObject>
#include <QString>
#include <QTimer>

// Forward declaration for hidapi
typedef struct hid_device_ hid_device;

#ifdef Q_OS_LINUX
class KpodUdevWorker;
class QThread;
#endif

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

class KpodDevice : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x04D8;
    static const quint16 PRODUCT_ID = 0xF12D;

    // Rocker switch positions (bits 5-6 of controls byte)
    enum RockerPosition {
        RockerCenter = 0, // 0b00 - VFO B / Center position
        RockerRight = 1,  // 0b01 - XIT/RIT position
        RockerLeft = 2    // 0b10 - VFO A position
    };
    Q_ENUM(RockerPosition)

    explicit KpodDevice(QObject *parent = nullptr);
    ~KpodDevice();

    // Detection
    bool isDetected() const;
    KpodDeviceInfo deviceInfo() const;
    static KpodDeviceInfo detectDevice();

    // Polling control
    bool startPolling();
    void stopPolling();
    bool isPolling() const;

    // Current state
    RockerPosition rockerPosition() const;

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady(); // Fired once after async detectDevice() completes at startup
    void encoderRotated(int ticks);
    void rockerPositionChanged(RockerPosition position);
    void buttonTapped(int buttonNumber); // Button 1-8 brief press
    void buttonHeld(int buttonNumber);   // Button 1-8 long press
    void pollError(const QString &error);

private slots:
    // Deferred from the constructor via QTimer::singleShot(0) — runs detectDevice()
    // after the event loop starts so the 400ms hid_open_path retry loop does not
    // freeze app startup. Emits deviceInfoReady() on completion.
    void initialize();
    void poll();
    void onDeviceArrived();
    void onDeviceRemoved();

private:
    bool openDevice();
    void closeDevice();
    void processResponse(const unsigned char *buffer);

    // Hotplug monitoring
    void setupHotplugMonitoring();
    void teardownHotplugMonitoring();

    KpodDeviceInfo m_deviceInfo;
    hid_device *m_hidDevice = nullptr;
    QTimer *m_pollTimer = nullptr;
    static const int POLL_INTERVAL_MS = 20;
    RockerPosition m_lastRockerPosition = RockerCenter;
    quint8 m_lastButtonState = 0; // Button number (1-8) or 0 for none
    bool m_holdEmitted = false;   // Track if hold signal was already emitted for current press

    // Hotplug monitoring.
    // macOS / Windows: periodic hid_enumerate() — we use this instead of IOKit callbacks
    //   because hidapi internally uses IOHIDManager on macOS, and two managers conflict.
    //   On those platforms hid_enumerate() reads cached USB descriptors from the OS and is
    //   cheap.
    // Linux: kernel-driven udev USB hotplug events on a worker thread. hid_enumerate() on
    //   Linux is implemented via libusb_get_device_list() and is far heavier than on
    //   macOS/Windows; polling it dominated idle CPU on the user's laptop.
#ifdef Q_OS_LINUX
    KpodUdevWorker *m_udevWorker = nullptr;
    QThread *m_udevThread = nullptr;
#else
    QTimer *m_presenceTimer = nullptr;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;
    void checkDevicePresence();
#endif
};

#endif // KPODDEVICE_H
