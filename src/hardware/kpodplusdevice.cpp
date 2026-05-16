#include "kpodplusdevice.h"
#include <libusb-1.0/libusb.h>
#include <QLoggingCategory>

#ifdef Q_OS_LINUX
#include "kpodudevworker.h"
#endif

Q_LOGGING_CATEGORY(hwKpodPlus, "hw.kpodplus")

static void kpodPlusLog(const QString &msg) {
    qCDebug(hwKpodPlus) << "KPOD+:" << msg;
}

// =============================================================================
// EP02 Worker — dedicated thread for blocking keyer reads
// =============================================================================

KpodPlusEp02Worker::KpodPlusEp02Worker(QObject *parent) : QObject(parent) {}

void KpodPlusEp02Worker::setDeviceHandle(libusb_device_handle *handle) {
    m_handle = handle;
}

void KpodPlusEp02Worker::requestStop() {
    m_running.store(false, std::memory_order_relaxed);
}

void KpodPlusEp02Worker::run() {
    m_running.store(true, std::memory_order_relaxed);
    unsigned char buffer[32];

    while (m_running.load(std::memory_order_relaxed)) {
        int transferred = 0;
        // EP02 IN = 0x82, blocking with 10ms timeout
        int rc = libusb_interrupt_transfer(m_handle, 0x82, buffer, sizeof(buffer), &transferred, 10);

        if (rc == LIBUSB_SUCCESS && transferred > 0) {
            QByteArray data(reinterpret_cast<const char *>(buffer), transferred);
            emit keyerDataReceived(data);
        } else if (rc == LIBUSB_ERROR_TIMEOUT) {
            // Normal — no keyer data pending
            continue;
        } else if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
            qCWarning(hwKpodPlus) << "KPOD+ EP02: device error" << libusb_error_name(rc);
            break;
        }
        // Other errors (LIBUSB_ERROR_OVERFLOW etc.) — log and continue
        else if (rc != LIBUSB_SUCCESS) {
            qCDebug(hwKpodPlus) << "KPOD+ EP02: transfer error" << libusb_error_name(rc);
        }
    }
}

// =============================================================================
// KpodPlusDevice
// =============================================================================

KpodPlusDevice::KpodPlusDevice(QObject *parent) : QObject(parent), m_pollTimer(new QTimer(this)) {
    // Initialize libusb context
    int rc = libusb_init(&m_usbContext);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "KPOD+: libusb_init failed:" << libusb_error_name(rc);
        m_usbContext = nullptr;
    }

    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &KpodPlusDevice::poll);

    setupHotplugMonitoring();

    // Deferred detection (same pattern as KpodDevice)
    QTimer::singleShot(0, this, &KpodPlusDevice::initialize);
}

void KpodPlusDevice::initialize() {
    m_deviceInfo = detectDevice();
    emit deviceInfoReady();
}

KpodPlusDevice::~KpodPlusDevice() {
    stopPolling();
    teardownHotplugMonitoring();
    if (m_usbContext) {
        libusb_exit(m_usbContext);
    }
}

bool KpodPlusDevice::isDetected() const {
    return m_deviceInfo.detected;
}

KpodPlusDeviceInfo KpodPlusDevice::deviceInfo() const {
    return m_deviceInfo;
}

bool KpodPlusDevice::startPolling() {
    if (m_pollTimer->isActive()) {
        return true;
    }

    if (!openDevice()) {
        emit pollError("Failed to open KPOD+ device");
        return false;
    }

    m_lastRockerPosition = RockerCenter;
    m_lastButtonState = 0;
    m_holdEmitted = false;

    // Start EP02 keyer reader thread
    startEp02Reader();

    m_pollTimer->start();
    emit deviceConnected();
    return true;
}

void KpodPlusDevice::stopPolling() {
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
    stopEp02Reader();
    closeDevice();
}

bool KpodPlusDevice::isPolling() const {
    return m_pollTimer->isActive();
}

KpodPlusDevice::RockerPosition KpodPlusDevice::rockerPosition() const {
    return m_lastRockerPosition;
}

// =============================================================================
// Keyer Configuration Commands (sent via EP01 OUT)
// =============================================================================

void KpodPlusDevice::setKeyerSpeed(int wpm) {
    wpm = qBound(8, wpm, 100);
    unsigned char cmd[8] = {'K', 0x03, static_cast<unsigned char>(wpm), 0, 0, 0, 0, 0};
    sendCommand(cmd, sizeof(cmd));
}

void KpodPlusDevice::setCwPitch(int freqHz) {
    // Convert Hz to tens-of-Hz (400-1000 Hz → 40-100)
    int tenHz = qBound(40, freqHz / 10, 100);
    unsigned char cmd[8] = {'K', 0x04, static_cast<unsigned char>(tenHz), 0, 0, 0, 0, 0};
    sendCommand(cmd, sizeof(cmd));
}

void KpodPlusDevice::setKeyerParams(int iambicMode, bool paddleReversed) {
    unsigned char mode = static_cast<unsigned char>(qBound(0, iambicMode, 1));
    unsigned char orient = paddleReversed ? 1 : 0;
    unsigned char cmd[8] = {'K', 0x01, mode, orient, 0, 0, 0, 0};
    sendCommand(cmd, sizeof(cmd));
}

void KpodPlusDevice::setEncodeMode(int mode) {
    unsigned char m = static_cast<unsigned char>(qBound(0, mode, 1));
    unsigned char cmd[8] = {'K', 0x02, m, 0, 0, 0, 0, 0};
    sendCommand(cmd, sizeof(cmd));
}

void KpodPlusDevice::setStuckTimeout(int seconds) {
    seconds = qBound(5, seconds, 600);
    unsigned char lo = static_cast<unsigned char>(seconds & 0xFF);
    unsigned char hi = static_cast<unsigned char>((seconds >> 8) & 0xFF);
    unsigned char cmd[8] = {'K', 0x05, lo, hi, 0, 0, 0, 0};
    sendCommand(cmd, sizeof(cmd));
}

// =============================================================================
// USB Communication
// =============================================================================

bool KpodPlusDevice::openDevice() {
    if (m_usbHandle) {
        return true;
    }
    if (!m_usbContext || !m_deviceInfo.detected) {
        return false;
    }

    // Find device by bus/address from detection
    libusb_device **devList = nullptr;
    ssize_t cnt = libusb_get_device_list(m_usbContext, &devList);
    if (cnt < 0) {
        return false;
    }

    libusb_device *targetDev = nullptr;
    for (ssize_t i = 0; i < cnt; i++) {
        if (libusb_get_bus_number(devList[i]) == m_deviceInfo.busNumber &&
            libusb_get_device_address(devList[i]) == m_deviceInfo.deviceAddress) {
            targetDev = devList[i];
            break;
        }
    }

    if (!targetDev) {
        libusb_free_device_list(devList, 1);
        qCWarning(hwKpodPlus) << "KPOD+: device not found at bus" << m_deviceInfo.busNumber << "addr"
                              << m_deviceInfo.deviceAddress;
        return false;
    }

    int rc = libusb_open(targetDev, &m_usbHandle);
    libusb_free_device_list(devList, 1);

    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "KPOD+: libusb_open failed:" << libusb_error_name(rc);
        m_usbHandle = nullptr;
        return false;
    }

    // Detach kernel driver if active (Linux)
#ifdef Q_OS_LINUX
    if (libusb_kernel_driver_active(m_usbHandle, 0) == 1) {
        libusb_detach_kernel_driver(m_usbHandle, 0);
    }
#endif

    rc = libusb_claim_interface(m_usbHandle, 0);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "KPOD+: claim interface failed:" << libusb_error_name(rc);
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
        return false;
    }
    m_interfaceClaimed = true;

    kpodPlusLog("Device opened and interface claimed");
    return true;
}

void KpodPlusDevice::closeDevice() {
    if (m_usbHandle) {
        if (m_interfaceClaimed) {
            libusb_release_interface(m_usbHandle, 0);
            m_interfaceClaimed = false;
        }
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
        emit deviceDisconnected();
    }
}

bool KpodPlusDevice::sendCommand(const unsigned char *data, int length) {
    if (!m_usbHandle) {
        return false;
    }

    int transferred = 0;
    // EP01 OUT write
    int rc = libusb_interrupt_transfer(m_usbHandle, 0x01, const_cast<unsigned char *>(data), length, &transferred, 100);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "KPOD+: EP01 write failed:" << libusb_error_name(rc);
        return false;
    }

    // EP01 IN read-back (completes the transaction pair per Elecraft protocol)
    unsigned char response[8] = {};
    int respTransferred = 0;
    rc = libusb_interrupt_transfer(m_usbHandle, 0x81, response, sizeof(response), &respTransferred, 100);
    if (rc == LIBUSB_SUCCESS && respTransferred > 0) {
        qCDebug(hwKpodPlus) << "KPOD+: EP01 response"
                            << QByteArray(reinterpret_cast<char *>(response), respTransferred).toHex(' ');
    }
    // Timeout is acceptable — read was still issued to drain device buffer

    return true;
}

void KpodPlusDevice::poll() {
    if (!m_usbHandle) {
        stopPolling();
        emit pollError("Device handle invalid");
        return;
    }

    // Send 'u' (update request) via EP01 OUT
    unsigned char cmd[8] = {'u', 0, 0, 0, 0, 0, 0, 0};
    int transferred = 0;
    int rc = libusb_interrupt_transfer(m_usbHandle, 0x01, cmd, sizeof(cmd), &transferred, 50);

    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
        qCWarning(hwKpodPlus) << "KPOD+: EP01 write failed:" << libusb_error_name(rc);
        stopPolling();
        emit pollError("Failed to write to KPOD+");
        return;
    }

    // Read response from EP01 IN = 0x81
    unsigned char buffer[8];
    transferred = 0;
    rc = libusb_interrupt_transfer(m_usbHandle, 0x81, buffer, sizeof(buffer), &transferred, 5);

    if (rc == LIBUSB_SUCCESS && transferred == 8) {
        processResponse(buffer);
    } else if (rc == LIBUSB_ERROR_TIMEOUT) {
        // No data — normal for non-blocking equivalent
    } else if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
        qCWarning(hwKpodPlus) << "KPOD+: EP01 read failed:" << libusb_error_name(rc);
        stopPolling();
        emit pollError("Failed to read from KPOD+");
    }
}

void KpodPlusDevice::processResponse(const unsigned char *buffer) {
    // Same protocol as KPOD:
    // buffer[0] = cmd: 'u' if new event, 0 if no event
    // buffer[1-2] = ticks: signed 16-bit encoder count (little-endian)
    // buffer[3] = controls: button/tap-hold/rocker state
    // buffer[4-7] = spare

    unsigned char cmd = buffer[0];

    if (cmd != 'u') {
        if (m_lastButtonState != 0) {
            if (!m_holdEmitted) {
                emit buttonTapped(m_lastButtonState);
            }
            m_lastButtonState = 0;
            m_holdEmitted = false;
        }
        return;
    }

    qint16 encoderTicks = static_cast<qint16>(buffer[1] | (buffer[2] << 8));
    quint8 controls = buffer[3];

    quint8 buttonNum = controls & 0x0F;
    bool isHold = (controls >> 4) & 0x01;
    RockerPosition rocker = static_cast<RockerPosition>((controls >> 5) & 0x03);

    if (encoderTicks != 0) {
        emit encoderRotated(encoderTicks);
    }

    // Button state machine (identical to KpodDevice)
    if (buttonNum != 0 && m_lastButtonState == 0) {
        m_lastButtonState = buttonNum;
        m_holdEmitted = false;
        if (isHold) {
            emit buttonHeld(buttonNum);
            m_holdEmitted = true;
        }
    } else if (buttonNum != 0 && m_lastButtonState != 0) {
        if (isHold && !m_holdEmitted) {
            emit buttonHeld(m_lastButtonState);
            m_holdEmitted = true;
        }
    } else if (buttonNum == 0 && m_lastButtonState != 0) {
        if (!m_holdEmitted) {
            emit buttonTapped(m_lastButtonState);
        }
        m_lastButtonState = 0;
        m_holdEmitted = false;
    }

    if (rocker != m_lastRockerPosition && rocker != 3) {
        m_lastRockerPosition = rocker;
        emit rockerPositionChanged(rocker);
    }
}

// =============================================================================
// EP02 Reader Thread
// =============================================================================

void KpodPlusDevice::startEp02Reader() {
    if (m_ep02Thread) {
        return;
    }

    m_ep02Worker = new KpodPlusEp02Worker(nullptr);
    m_ep02Worker->setDeviceHandle(m_usbHandle);
    m_ep02Thread = new QThread(this);
    m_ep02Thread->setObjectName("KpodPlusEP02");
    m_ep02Worker->moveToThread(m_ep02Thread);

    connect(m_ep02Thread, &QThread::started, m_ep02Worker, &KpodPlusEp02Worker::run);
    connect(m_ep02Worker, &KpodPlusEp02Worker::keyerDataReceived, this, &KpodPlusDevice::keyerDataReceived,
            Qt::QueuedConnection);
    connect(m_ep02Thread, &QThread::finished, m_ep02Worker, &QObject::deleteLater);

    m_ep02Thread->start();
}

void KpodPlusDevice::stopEp02Reader() {
    if (m_ep02Worker) {
        m_ep02Worker->requestStop();
    }
    if (m_ep02Thread) {
        m_ep02Thread->quit();
        m_ep02Thread->wait(2000);
        m_ep02Thread->deleteLater();
        m_ep02Thread = nullptr;
    }
    m_ep02Worker = nullptr;
}

// =============================================================================
// Detection — libusb scan for vendor-specific class device
// =============================================================================

KpodPlusDeviceInfo KpodPlusDevice::detectDevice() {
    KpodPlusDeviceInfo info;

    kpodPlusLog("detectDevice() starting");

    // Static method — uses its own short-lived context (callable without an instance)
    libusb_context *ctx = nullptr;
    int rc = libusb_init(&ctx);
    if (rc != LIBUSB_SUCCESS) {
        kpodPlusLog(QString("libusb_init failed: %1").arg(libusb_error_name(rc)));
        return info;
    }

    libusb_device **devList = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &devList);
    if (cnt < 0) {
        kpodPlusLog("libusb_get_device_list failed");
        libusb_exit(ctx);
        return info;
    }

    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device *dev = devList[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS) {
            continue;
        }

        // Check VID
        if (desc.idVendor != VENDOR_ID) {
            continue;
        }

        // Check PID (current production or future Elecraft PID)
        if (desc.idProduct != PRODUCT_ID_KPOD && desc.idProduct != PRODUCT_ID_ELECRAFT) {
            continue;
        }

        // Check for vendor-specific interface class (255)
        struct libusb_config_descriptor *config = nullptr;
        if (libusb_get_active_config_descriptor(dev, &config) != LIBUSB_SUCCESS) {
            continue;
        }

        bool hasVendorInterface = false;
        for (int iface = 0; iface < config->bNumInterfaces; iface++) {
            const struct libusb_interface *interface = &config->interface[iface];
            for (int alt = 0; alt < interface->num_altsetting; alt++) {
                if (interface->altsetting[alt].bInterfaceClass == VENDOR_INTERFACE_CLASS) {
                    hasVendorInterface = true;
                    break;
                }
            }
            if (hasVendorInterface)
                break;
        }
        libusb_free_config_descriptor(config);

        if (!hasVendorInterface) {
            continue;
        }

        // Found a KPOD+ device
        info.detected = true;
        info.vendorId = desc.idVendor;
        info.productId = desc.idProduct;
        info.busNumber = libusb_get_bus_number(dev);
        info.deviceAddress = libusb_get_device_address(dev);

        kpodPlusLog(QString("Found KPOD+ at bus %1 addr %2 (VID=0x%3, PID=0x%4)")
                        .arg(info.busNumber)
                        .arg(info.deviceAddress)
                        .arg(info.vendorId, 4, 16, QChar('0'))
                        .arg(info.productId, 4, 16, QChar('0')));

        // Open device to read string descriptors and query firmware/ID
        libusb_device_handle *handle = nullptr;
        if (libusb_open(dev, &handle) == LIBUSB_SUCCESS) {
            unsigned char strBuf[256];

            // Product name
            if (desc.iProduct > 0) {
                int len = libusb_get_string_descriptor_ascii(handle, desc.iProduct, strBuf, sizeof(strBuf));
                if (len > 0) {
                    info.productName = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
                }
            }

            // Manufacturer
            if (desc.iManufacturer > 0) {
                int len = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, strBuf, sizeof(strBuf));
                if (len > 0) {
                    info.manufacturer = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
                }
            }

            // Claim interface temporarily to query device ID and firmware version
#ifdef Q_OS_LINUX
            if (libusb_kernel_driver_active(handle, 0) == 1) {
                libusb_detach_kernel_driver(handle, 0);
            }
#endif
            if (libusb_claim_interface(handle, 0) == LIBUSB_SUCCESS) {
                unsigned char cmdBuf[8];
                unsigned char readBuf[8];
                int transferred = 0;

                // Query Device ID ('=')
                memset(cmdBuf, 0, sizeof(cmdBuf));
                cmdBuf[0] = '=';
                rc = libusb_interrupt_transfer(handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100);
                if (rc == LIBUSB_SUCCESS) {
                    transferred = 0;
                    rc = libusb_interrupt_transfer(handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100);
                    if (rc == LIBUSB_SUCCESS && transferred == 8) {
                        QString idStr;
                        for (int j = 1; j < 8 && readBuf[j] != 0; ++j) {
                            idStr += QChar(readBuf[j]);
                        }
                        info.deviceId = idStr.trimmed();
                        kpodPlusLog(QString("Device ID: '%1'").arg(info.deviceId));
                    }
                }

                // Query Firmware Version ('v')
                memset(cmdBuf, 0, sizeof(cmdBuf));
                cmdBuf[0] = 'v';
                transferred = 0;
                rc = libusb_interrupt_transfer(handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100);
                if (rc == LIBUSB_SUCCESS) {
                    transferred = 0;
                    rc = libusb_interrupt_transfer(handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100);
                    if (rc == LIBUSB_SUCCESS && transferred == 8) {
                        qint16 versionBcd = static_cast<qint16>(readBuf[1] | (readBuf[2] << 8));
                        int major = versionBcd / 100;
                        int minor = versionBcd % 100;
                        info.firmwareVersion = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                        kpodPlusLog(QString("Firmware version: %1").arg(info.firmwareVersion));
                    }
                }

                libusb_release_interface(handle, 0);
            }

            libusb_close(handle);
        }

        break; // Found our device
    }

    libusb_free_device_list(devList, 1);
    libusb_exit(ctx);

    kpodPlusLog(QString("detectDevice() complete - detected=%1, version=%2, id=%3")
                    .arg(info.detected)
                    .arg(info.firmwareVersion)
                    .arg(info.deviceId));

    return info;
}

// =============================================================================
// Hotplug Monitoring
// =============================================================================

#ifdef Q_OS_LINUX

void KpodPlusDevice::setupHotplugMonitoring() {
    // Extend udev worker to also watch for KPOD+ (same VID, different interface class).
    // The udev worker watches by VID/PID at the usb_device level, which catches both
    // HID and vendor-specific class devices. The class filtering happens in detectDevice().
    m_udevWorker = new KpodUdevWorker(VENDOR_ID, PRODUCT_ID_KPOD);
    m_udevThread = new QThread(this);
    m_udevThread->setObjectName("KpodPlusUdev");
    m_udevWorker->moveToThread(m_udevThread);

    connect(m_udevThread, &QThread::started, m_udevWorker, &KpodUdevWorker::start);
    connect(m_udevWorker, &KpodUdevWorker::deviceArrived, this, &KpodPlusDevice::onDeviceArrived, Qt::QueuedConnection);
    connect(m_udevWorker, &KpodUdevWorker::deviceRemoved, this, &KpodPlusDevice::onDeviceRemoved, Qt::QueuedConnection);
    connect(m_udevThread, &QThread::finished, m_udevWorker, &QObject::deleteLater);

    m_udevThread->start();
}

void KpodPlusDevice::teardownHotplugMonitoring() {
    if (m_udevWorker) {
        m_udevWorker->stop();
    }
    if (m_udevThread) {
        m_udevThread->quit();
        m_udevThread->wait(2000);
        m_udevThread->deleteLater();
        m_udevThread = nullptr;
    }
    m_udevWorker = nullptr;
}

#else // macOS / Windows

void KpodPlusDevice::setupHotplugMonitoring() {
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &KpodPlusDevice::checkDevicePresence);
    m_presenceTimer->start();
}

void KpodPlusDevice::teardownHotplugMonitoring() {
    if (m_presenceTimer) {
        m_presenceTimer->stop();
        delete m_presenceTimer;
        m_presenceTimer = nullptr;
    }
}

void KpodPlusDevice::checkDevicePresence() {
    KpodPlusDeviceInfo probeInfo = detectDevice();
    bool nowDetected = probeInfo.detected;
    bool wasDetected = m_deviceInfo.detected;

    if (!wasDetected && nowDetected) {
        onDeviceArrived();
    } else if (wasDetected && !nowDetected) {
        onDeviceRemoved();
    }
}

#endif

void KpodPlusDevice::onDeviceArrived() {
    m_deviceInfo = detectDevice();
    emit deviceConnected();
}

void KpodPlusDevice::onDeviceRemoved() {
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
    stopEp02Reader();

    if (m_usbHandle) {
        if (m_interfaceClaimed) {
            libusb_release_interface(m_usbHandle, 0);
            m_interfaceClaimed = false;
        }
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
    }

    m_deviceInfo.detected = false;
    emit deviceDisconnected();
}
