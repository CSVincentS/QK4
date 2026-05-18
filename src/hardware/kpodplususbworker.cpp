#include "kpodplususbworker.h"
#include <QLoggingCategory>
#include <QString>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <cstring>
#include <libusb-1.0/libusb.h>

// WHY: libusb 1.0.21 introduced reliable hotplug on macOS and is the floor
// every other capability we depend on (interrupt event handler, multi-context
// safety). The Linux pkg-config check in CMakeLists.txt enforces this at
// configure time on POSIX; this compile-time gate covers Windows where
// pkg-config is unavailable.
static_assert(LIBUSB_API_VERSION >= 0x01000105, "libusb >= 1.0.21 required");

Q_LOGGING_CATEGORY(hwKpodPlus, "hw.kpodplus")

// =============================================================================
// Pure helpers
// =============================================================================

void KpodPlusUsbWorker::buildKeyerSpeedCmd(int wpm, unsigned char out[8]) {
    wpm = qBound(8, wpm, 100);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x03;
    out[2] = static_cast<unsigned char>(wpm);
}

void KpodPlusUsbWorker::buildCwPitchCmd(int freqHz, unsigned char out[8]) {
    int tenHz = qBound(40, freqHz / 10, 100);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x04;
    out[2] = static_cast<unsigned char>(tenHz);
}

void KpodPlusUsbWorker::buildKeyerParamsCmd(int iambicMode, bool reversed, unsigned char out[8]) {
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x01;
    out[2] = static_cast<unsigned char>(qBound(0, iambicMode, 1));
    out[3] = reversed ? 1 : 0;
}

void KpodPlusUsbWorker::buildEncodeModeCmd(int mode, unsigned char out[8]) {
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x02;
    out[2] = static_cast<unsigned char>(qBound(0, mode, 1));
}

void KpodPlusUsbWorker::buildStuckTimeoutCmd(int seconds, unsigned char out[8]) {
    seconds = qBound(5, seconds, 600);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x05;
    out[2] = static_cast<unsigned char>(seconds & 0xFF);
    out[3] = static_cast<unsigned char>((seconds >> 8) & 0xFF);
}

QByteArray KpodPlusUsbWorker::trimEp02Buffer(const QByteArray &raw) {
    int len = raw.size();
    while (len > 0 && raw.at(len - 1) == '\0')
        --len;
    return raw.left(len);
}

// =============================================================================
// EP01 response decoder
// =============================================================================

void KpodPlusUsbWorker::resetDecoderState() {
    m_lastRockerPosition = 0;
    m_lastButtonState = 0;
    m_holdEmitted = false;
}

KpodPlusUsbWorker::ResponseEvents KpodPlusUsbWorker::decodeResponse(const unsigned char buffer[8]) {
    ResponseEvents ev;
    const unsigned char cmd = buffer[0];

    if (cmd != 'u') {
        // No event reported this cycle. If a button was held down on the
        // previous cycle, treat the implicit no-op as a release (tap).
        if (m_lastButtonState != 0) {
            if (!m_holdEmitted) {
                ev.emitButtonTap = true;
                ev.buttonNum = m_lastButtonState;
            }
            m_lastButtonState = 0;
            m_holdEmitted = false;
        }
        return ev;
    }

    const qint16 ticks = static_cast<qint16>(buffer[1] | (buffer[2] << 8));
    const quint8 controls = buffer[3];
    const quint8 buttonNum = controls & 0x0F;
    const bool isHold = (controls >> 4) & 0x01;
    const int rocker = (controls >> 5) & 0x03;

    if (ticks != 0) {
        ev.emitEncoder = true;
        ev.encoderTicks = ticks;
    }

    if (buttonNum != 0 && m_lastButtonState == 0) {
        m_lastButtonState = buttonNum;
        m_holdEmitted = false;
        if (isHold) {
            ev.emitButtonHold = true;
            ev.buttonNum = buttonNum;
            m_holdEmitted = true;
        }
    } else if (buttonNum != 0 && m_lastButtonState != 0) {
        if (isHold && !m_holdEmitted) {
            ev.emitButtonHold = true;
            ev.buttonNum = m_lastButtonState;
            m_holdEmitted = true;
        }
    } else if (buttonNum == 0 && m_lastButtonState != 0) {
        if (!m_holdEmitted) {
            ev.emitButtonTap = true;
            ev.buttonNum = m_lastButtonState;
        }
        m_lastButtonState = 0;
        m_holdEmitted = false;
    }

    if (rocker != m_lastRockerPosition && rocker != 3) {
        m_lastRockerPosition = rocker;
        ev.emitRocker = true;
        ev.rockerPosition = rocker;
    }

    return ev;
}

// =============================================================================
// Lifecycle
// =============================================================================

KpodPlusUsbWorker::KpodPlusUsbWorker(QObject *parent) : QObject(parent) {}

KpodPlusUsbWorker::~KpodPlusUsbWorker() {
    // The façade is expected to invoke shutdown() via BlockingQueuedConnection
    // before destroying the thread. If it didn't, fall back here.
    if (m_handle || m_ctx) {
        releaseHandle();
        if (m_ctx) {
            libusb_exit(m_ctx);
            m_ctx = nullptr;
        }
    }
}

void KpodPlusUsbWorker::start() {
    const int rc = libusb_init(&m_ctx);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "libusb_init failed:" << libusb_error_name(rc);
        m_ctx = nullptr;
        return;
    }

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, &KpodPlusUsbWorker::onPollTimer);

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &KpodPlusUsbWorker::onPresenceTimer);
    m_presenceTimer->start();

    // Initial detection on the worker thread (formerly a deferred singleShot
    // on the main thread that could stall the GUI).
    KpodPlusDeviceInfo info;
    if (detectDeviceLocation(&info)) {
        m_devicePresent = true;
    }
    m_info = info;
    emit deviceInfoReady(m_info);
}

void KpodPlusUsbWorker::shutdown() {
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    if (m_presenceTimer) {
        m_presenceTimer->stop();
    }
    releaseHandle();
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

// =============================================================================
// Detection / open
// =============================================================================

bool KpodPlusUsbWorker::detectDeviceLocation(KpodPlusDeviceInfo *info) {
    if (!m_ctx)
        return false;

    libusb_device **devList = nullptr;
    const ssize_t cnt = libusb_get_device_list(m_ctx, &devList);
    if (cnt < 0)
        return false;

    bool found = false;
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device *dev = devList[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
            continue;
        if (desc.idVendor != VENDOR_ID)
            continue;
        if (desc.idProduct != PRODUCT_ID_KPOD && desc.idProduct != PRODUCT_ID_ELECRAFT)
            continue;

        // WHY: enumerate-only. Walking config descriptors is cache-only; we
        // intentionally do NOT open the device here. The earlier code opened
        // it every 2 s on the main thread to query firmware/ID, which stressed
        // the USB stack and competed with the active poll session.
        struct libusb_config_descriptor *config = nullptr;
        if (libusb_get_active_config_descriptor(dev, &config) != LIBUSB_SUCCESS)
            continue;
        bool hasVendor = false;
        for (int iface = 0; iface < config->bNumInterfaces && !hasVendor; ++iface) {
            for (int alt = 0; alt < config->interface[iface].num_altsetting; ++alt) {
                if (config->interface[iface].altsetting[alt].bInterfaceClass == VENDOR_INTERFACE_CLASS) {
                    hasVendor = true;
                    break;
                }
            }
        }
        libusb_free_config_descriptor(config);
        if (!hasVendor)
            continue;

        info->detected = true;
        info->vendorId = desc.idVendor;
        info->productId = desc.idProduct;
        info->busNumber = libusb_get_bus_number(dev);
        info->deviceAddress = libusb_get_device_address(dev);
        found = true;
        break;
    }

    libusb_free_device_list(devList, 1);
    return found;
}

bool KpodPlusUsbWorker::queryOpenDeviceInfo(KpodPlusDeviceInfo *info) {
    if (!m_handle)
        return false;

    libusb_device *dev = libusb_get_device(m_handle);
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
        return false;

    unsigned char strBuf[256];
    if (desc.iProduct > 0) {
        int len = libusb_get_string_descriptor_ascii(m_handle, desc.iProduct, strBuf, sizeof(strBuf));
        if (len > 0)
            info->productName = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
    }
    if (desc.iManufacturer > 0) {
        int len = libusb_get_string_descriptor_ascii(m_handle, desc.iManufacturer, strBuf, sizeof(strBuf));
        if (len > 0)
            info->manufacturer = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
    }

    unsigned char cmdBuf[8];
    unsigned char readBuf[8];
    int transferred = 0;

    // Device ID ('=')
    std::memset(cmdBuf, 0, sizeof(cmdBuf));
    cmdBuf[0] = '=';
    if (libusb_interrupt_transfer(m_handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100) == LIBUSB_SUCCESS) {
        transferred = 0;
        if (libusb_interrupt_transfer(m_handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100) == LIBUSB_SUCCESS &&
            transferred == 8) {
            QString idStr;
            for (int j = 1; j < 8 && readBuf[j] != 0; ++j)
                idStr += QChar(readBuf[j]);
            info->deviceId = idStr.trimmed();
        }
    }

    // Firmware version ('v') — BCD
    std::memset(cmdBuf, 0, sizeof(cmdBuf));
    cmdBuf[0] = 'v';
    transferred = 0;
    if (libusb_interrupt_transfer(m_handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100) == LIBUSB_SUCCESS) {
        transferred = 0;
        if (libusb_interrupt_transfer(m_handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100) == LIBUSB_SUCCESS &&
            transferred == 8) {
            const qint16 versionBcd = static_cast<qint16>(readBuf[1] | (readBuf[2] << 8));
            const int major = versionBcd / 100;
            const int minor = versionBcd % 100;
            info->firmwareVersion = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
        }
    }

    return true;
}

bool KpodPlusUsbWorker::openHandle() {
    if (m_handle)
        return true;
    if (!m_ctx || !m_info.detected)
        return false;

    libusb_device **devList = nullptr;
    const ssize_t cnt = libusb_get_device_list(m_ctx, &devList);
    if (cnt < 0)
        return false;

    libusb_device *target = nullptr;
    for (ssize_t i = 0; i < cnt; ++i) {
        if (libusb_get_bus_number(devList[i]) == m_info.busNumber &&
            libusb_get_device_address(devList[i]) == m_info.deviceAddress) {
            target = devList[i];
            break;
        }
    }
    if (!target) {
        libusb_free_device_list(devList, 1);
        qCWarning(hwKpodPlus) << "openHandle: device not found at bus" << m_info.busNumber << "addr"
                              << m_info.deviceAddress;
        return false;
    }

    int rc = libusb_open(target, &m_handle);
    libusb_free_device_list(devList, 1);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "libusb_open failed:" << libusb_error_name(rc);
        m_handle = nullptr;
        return false;
    }

#ifdef Q_OS_LINUX
    if (libusb_kernel_driver_active(m_handle, 0) == 1) {
        libusb_detach_kernel_driver(m_handle, 0);
    }
#endif
    rc = libusb_claim_interface(m_handle, 0);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "claim_interface failed:" << libusb_error_name(rc);
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }
    m_interfaceClaimed = true;
    return true;
}

void KpodPlusUsbWorker::releaseHandle() {
    if (m_handle) {
        emit handleClosing();
        if (m_interfaceClaimed) {
            libusb_release_interface(m_handle, 0);
            m_interfaceClaimed = false;
        }
        libusb_close(m_handle);
        m_handle = nullptr;
    }
}

void KpodPlusUsbWorker::openDevice() {
    if (m_handle) {
        return;
    }
    if (!openHandle()) {
        emit pollError(QStringLiteral("Failed to open KPOD+ device"));
        return;
    }
    resetDecoderState();
    queryOpenDeviceInfo(&m_info);
    emit deviceInfoReady(m_info);
    emit handleOpened(reinterpret_cast<quintptr>(m_handle));
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    emit deviceArrived();
}

void KpodPlusUsbWorker::closeDevice() {
    const bool wasPolling = (m_pollTimer && m_pollTimer->isActive());
    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();
    // Surface a user-visible "device no longer keying" event so the façade
    // can clear m_polling and downstream listeners (KPA1500 mini-panel,
    // KpodPage status row) refresh. Distinct from the hotplug-yank case
    // which still goes via the presence timer.
    if (wasPolling)
        emit deviceRemoved();
}

// =============================================================================
// EP01 polling + helpers
// =============================================================================

bool KpodPlusUsbWorker::sendEp01Out(const unsigned char data[8]) {
    if (!m_handle)
        return false;
    int transferred = 0;
    const int rc = libusb_interrupt_transfer(m_handle, 0x01, const_cast<unsigned char *>(data), 8, &transferred, 50);
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
        qCWarning(hwKpodPlus) << "EP01 OUT failed:" << libusb_error_name(rc);
        if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO)
            handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
        return false;
    }
    return true;
}

bool KpodPlusUsbWorker::readEp01In(unsigned char buffer[8], int timeoutMs) {
    if (!m_handle)
        return false;
    int transferred = 0;
    const int rc = libusb_interrupt_transfer(m_handle, 0x81, buffer, 8, &transferred, timeoutMs);
    if (rc == LIBUSB_SUCCESS && transferred == 8)
        return true;
    if (rc == LIBUSB_ERROR_TIMEOUT)
        return false;
    if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
        qCWarning(hwKpodPlus) << "EP01 IN failed:" << libusb_error_name(rc);
        handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
    }
    return false;
}

void KpodPlusUsbWorker::handleLostDevice(QString reason) {
    // Idempotent: handleLostDevice can be invoked from three paths during a
    // single USB transient — sendEp01Out failure, readEp01In failure, and the
    // queued connection from EP02 worker's transferError. Bail if we've
    // already torn down so we don't double-emit signals or re-schedule
    // closeDevice.
    if (!m_devicePresent && !m_handle)
        return;
    qCWarning(hwKpodPlus) << "Device lost:" << reason;
    m_devicePresent = false;
    // Reset cached info so the page header clears immediately. The presence
    // timer re-fills it on its next tick if the device recovers.
    m_info = KpodPlusDeviceInfo{};
    emit deviceInfoReady(m_info);
    QTimer::singleShot(0, this, &KpodPlusUsbWorker::closeDevice);
}

void KpodPlusUsbWorker::onPollTimer() {
    if (!m_handle)
        return;

    unsigned char cmd[8] = {'u', 0, 0, 0, 0, 0, 0, 0};
    if (!sendEp01Out(cmd))
        return;

    unsigned char buffer[8] = {};
    if (!readEp01In(buffer, 5))
        return;

    const ResponseEvents ev = decodeResponse(buffer);
    if (ev.emitEncoder)
        emit encoderRotated(ev.encoderTicks);
    if (ev.emitButtonHold)
        emit buttonHeld(ev.buttonNum);
    if (ev.emitButtonTap)
        emit buttonTapped(ev.buttonNum);
    if (ev.emitRocker)
        emit rockerPositionChanged(ev.rockerPosition);
}

void KpodPlusUsbWorker::onPresenceTimer() {
    // Enumerate-only. No libusb_open / no claim_interface — just walks the
    // cached USB topology and matches VID/PID. ~1 ms cost on macOS/Linux.
    KpodPlusDeviceInfo probe;
    const bool now = detectDeviceLocation(&probe);

    if (!m_devicePresent && now) {
        m_devicePresent = true;
        m_info = probe;
        emit deviceInfoReady(m_info);
        openDevice();
    } else if (m_devicePresent && !now) {
        m_devicePresent = false;
        // Reset cached info before closeDevice() so the façade's m_info
        // re-emits with detected=false. Without this, the page header
        // keeps showing "KPOD+ Detected" with stale firmware/ID values
        // even after the user pulls the cable.
        m_info = KpodPlusDeviceInfo{};
        emit deviceInfoReady(m_info);
        closeDevice();
        emit deviceRemoved();
    }
}

// =============================================================================
// Setters — sync EP01 transactions on the worker thread
// =============================================================================

static bool sendConfigCommand(libusb_device_handle *handle, const unsigned char cmd[8]) {
    if (!handle)
        return false;
    int transferred = 0;
    int rc = libusb_interrupt_transfer(handle, 0x01, const_cast<unsigned char *>(cmd), 8, &transferred, 100);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "config EP01 OUT failed:" << libusb_error_name(rc);
        return false;
    }
    // WHY drain the readback: the device responds to every EP01 OUT with an
    // 8-byte status frame. If we don't read it, it sits in the host's EP01 IN
    // FIFO and gets returned as the response to the next poll's 'u' command,
    // shifting all subsequent reads by one frame.
    unsigned char response[8] = {};
    int respTransferred = 0;
    libusb_interrupt_transfer(handle, 0x81, response, sizeof(response), &respTransferred, 100);
    return true;
}

void KpodPlusUsbWorker::setKeyerSpeed(int wpm) {
    unsigned char cmd[8];
    buildKeyerSpeedCmd(wpm, cmd);
    sendConfigCommand(m_handle, cmd);
}

void KpodPlusUsbWorker::setCwPitch(int freqHz) {
    unsigned char cmd[8];
    buildCwPitchCmd(freqHz, cmd);
    sendConfigCommand(m_handle, cmd);
}

void KpodPlusUsbWorker::setKeyerParams(int iambicMode, bool paddleReversed) {
    unsigned char cmd[8];
    buildKeyerParamsCmd(iambicMode, paddleReversed, cmd);
    sendConfigCommand(m_handle, cmd);
}

void KpodPlusUsbWorker::setEncodeMode(int mode) {
    unsigned char cmd[8];
    buildEncodeModeCmd(mode, cmd);
    sendConfigCommand(m_handle, cmd);
}

void KpodPlusUsbWorker::setStuckTimeout(int seconds) {
    unsigned char cmd[8];
    buildStuckTimeoutCmd(seconds, cmd);
    sendConfigCommand(m_handle, cmd);
}

// =============================================================================
// EP02 reader worker
// =============================================================================

KpodPlusEp02Worker::KpodPlusEp02Worker(QObject *parent) : QObject(parent) {}

void KpodPlusEp02Worker::setDeviceHandle(quintptr handle) {
    m_handle.store(reinterpret_cast<libusb_device_handle *>(handle), std::memory_order_release);
}

void KpodPlusEp02Worker::requestStop() {
    m_running.store(false, std::memory_order_relaxed);
}

void KpodPlusEp02Worker::run() {
    m_running.store(true, std::memory_order_relaxed);
    unsigned char buffer[32];

    // WHY a longer timeout than the legacy 10 ms: the device's EP02 FIFO is
    // multi-packet deep so we don't lose data during the wait. A short
    // timeout served only to check m_running, which we'd hit at worst once per
    // timeout window — 100 ms is plenty for clean shutdown perception.
    constexpr int kEp02TimeoutMs = 100;

    while (m_running.load(std::memory_order_relaxed)) {
        libusb_device_handle *h = m_handle.load(std::memory_order_acquire);
        if (!h) {
            // No device — sleep briefly and re-check. Using a short blocking
            // wait keeps shutdown latency low without burning CPU.
            QThread::msleep(20);
            continue;
        }

        int transferred = 0;
        const int rc = libusb_interrupt_transfer(h, 0x82, buffer, sizeof(buffer), &transferred, kEp02TimeoutMs);
        if (rc == LIBUSB_SUCCESS && transferred > 0) {
            QByteArray data(reinterpret_cast<const char *>(buffer), transferred);
            // Diagnostic: timestamp each KZ batch so on-air vs wire-out timing can be compared
            // when investigating "rhythm wrong at the K4" reports. Enable with
            // QT_LOGGING_RULES="hw.kpodplus.debug=true". Trimmed to drop trailing NULs (32-byte
            // fixed transfer per docs/KPodKeyerInterface.pdf §"Endpoint 2 IN").
            qCDebug(hwKpodPlus).noquote() << "KZ@" << QTime::currentTime().toString("HH:mm:ss.zzz") << "["
                                          << KpodPlusUsbWorker::trimEp02Buffer(data) << "]";
            emit keyerDataReceived(data);
        } else if (rc == LIBUSB_ERROR_TIMEOUT) {
            continue;
        } else if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
            emit transferError(QString::fromLatin1(libusb_error_name(rc)));
            // Clear handle so we don't keep poking a dead device; the façade
            // will set a fresh handle on the next deviceArrived.
            m_handle.store(nullptr, std::memory_order_release);
        }
    }
}
