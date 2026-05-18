#include "kpodplusdevice.h"
#include "kpodplususbworker.h"
#include <QThread>

KpodPlusDevice::KpodPlusDevice(QObject *parent) : QObject(parent) {
    // --- EP02 reader thread (HighPriority) -----------------------------------
    m_ep02Thread = new QThread(this);
    m_ep02Thread->setObjectName("KpodPlusEP02");
    m_ep02Worker = new KpodPlusEp02Worker();
    m_ep02Worker->moveToThread(m_ep02Thread);
    connect(m_ep02Thread, &QThread::started, m_ep02Worker, &KpodPlusEp02Worker::run);
    connect(m_ep02Thread, &QThread::finished, m_ep02Worker, &QObject::deleteLater);
    connect(m_ep02Worker, &KpodPlusEp02Worker::keyerDataReceived, this, &KpodPlusDevice::keyerDataReceived,
            Qt::DirectConnection);
    m_ep02Thread->start(QThread::HighPriority);

    // --- USB worker thread (default priority) --------------------------------
    m_usbThread = new QThread(this);
    m_usbThread->setObjectName("KpodPlusUSB");
    m_usbWorker = new KpodPlusUsbWorker();
    m_usbWorker->moveToThread(m_usbThread);
    // Cross-link: the USB worker's releaseHandle() acquires the EP02 worker's transfer
    // mutex before libusb_close so the close cannot race with an in-flight EP02 transfer.
    // Non-owning pointer; EP02 worker outlives the USB worker (EP02 stops first in dtor).
    m_usbWorker->setEp02TransferMutex(m_ep02Worker->transferMutex());
    connect(m_usbThread, &QThread::started, m_usbWorker, &KpodPlusUsbWorker::start);
    connect(m_usbThread, &QThread::finished, m_usbWorker, &QObject::deleteLater);

    // Cache + re-emit worker signals.
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceInfoReady, this, [this](KpodPlusDeviceInfo info) {
        m_info = info;
        emit deviceInfoReady();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceArrived, this, [this]() {
        m_polling = true;
        emit deviceConnected();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceRemoved, this, [this]() {
        m_polling = false;
        emit deviceDisconnected();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::encoderRotated, this, &KpodPlusDevice::encoderRotated);
    connect(m_usbWorker, &KpodPlusUsbWorker::rockerPositionChanged, this, [this](int position) {
        m_lastRocker = static_cast<RockerPosition>(position);
        emit rockerPositionChanged(m_lastRocker);
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::buttonTapped, this, &KpodPlusDevice::buttonTapped);
    connect(m_usbWorker, &KpodPlusUsbWorker::buttonHeld, this, &KpodPlusDevice::buttonHeld);
    connect(m_usbWorker, &KpodPlusUsbWorker::pollError, this, &KpodPlusDevice::pollError);

    // Route the live libusb_device_handle from the USB worker into the EP02
    // reader. setDeviceHandle is just an atomic store on m_ep02Worker.m_handle,
    // safe to invoke from any thread, so DirectConnection is intentional.
    //
    // WHY not QueuedConnection: m_ep02Worker::run() is a blocking sync-read
    // loop that never returns to its thread's event loop, so queued events to
    // m_ep02Worker would never be dispatched — the handle would stay null and
    // no keyer data would ever flow.
    connect(m_usbWorker, &KpodPlusUsbWorker::handleOpened, m_ep02Worker, &KpodPlusEp02Worker::setDeviceHandle,
            Qt::DirectConnection);
    connect(
        m_usbWorker, &KpodPlusUsbWorker::handleClosing, m_ep02Worker, [this]() { m_ep02Worker->setDeviceHandle(0); },
        Qt::DirectConnection);

    // EP02 read errors (NO_DEVICE / IO) on the EP02 thread propagate to the
    // USB worker so the device is closed and re-detected. Without this, an
    // EP02-only transient (e.g. one-endpoint glitch) would silently kill KZ
    // data forever while EP01 polling kept ticking — the user-visible
    // "paddles stopped registering after a while" symptom.
    connect(m_ep02Worker, &KpodPlusEp02Worker::transferError, m_usbWorker, &KpodPlusUsbWorker::handleLostDevice,
            Qt::QueuedConnection);

    m_usbThread->start();
}

KpodPlusDevice::~KpodPlusDevice() {
    // Stop EP02 reader first so it stops poking the libusb handle.
    if (m_ep02Worker) {
        m_ep02Worker->requestStop();
    }
    if (m_ep02Thread) {
        m_ep02Thread->quit();
        m_ep02Thread->wait(2000);
    }

    // Synchronously drain libusb on the worker thread, then quit it.
    if (m_usbWorker) {
        QMetaObject::invokeMethod(m_usbWorker, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (m_usbThread) {
        m_usbThread->quit();
        m_usbThread->wait(2000);
    }
}

bool KpodPlusDevice::isDetected() const {
    return m_info.detected;
}

KpodPlusDeviceInfo KpodPlusDevice::deviceInfo() const {
    return m_info;
}

bool KpodPlusDevice::isPolling() const {
    return m_polling;
}

KpodPlusDevice::RockerPosition KpodPlusDevice::rockerPosition() const {
    return m_lastRocker;
}

bool KpodPlusDevice::startPolling() {
    if (m_polling)
        return true;
    QMetaObject::invokeMethod(m_usbWorker, "openDevice", Qt::QueuedConnection);
    // Result is asynchronous; isPolling() becomes true on deviceArrived.
    return m_info.detected;
}

void KpodPlusDevice::stopPolling() {
    QMetaObject::invokeMethod(m_usbWorker, "closeDevice", Qt::QueuedConnection);
}

void KpodPlusDevice::setKeyerSpeed(int wpm) {
    QMetaObject::invokeMethod(m_usbWorker, "setKeyerSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
}

void KpodPlusDevice::setCwPitch(int freqHz) {
    QMetaObject::invokeMethod(m_usbWorker, "setCwPitch", Qt::QueuedConnection, Q_ARG(int, freqHz));
}

void KpodPlusDevice::setKeyerParams(int iambicMode, bool paddleReversed) {
    QMetaObject::invokeMethod(m_usbWorker, "setKeyerParams", Qt::QueuedConnection, Q_ARG(int, iambicMode),
                              Q_ARG(bool, paddleReversed));
}

void KpodPlusDevice::setEncodeMode(int mode) {
    QMetaObject::invokeMethod(m_usbWorker, "setEncodeMode", Qt::QueuedConnection, Q_ARG(int, mode));
}

void KpodPlusDevice::setStuckTimeout(int seconds) {
    QMetaObject::invokeMethod(m_usbWorker, "setStuckTimeout", Qt::QueuedConnection, Q_ARG(int, seconds));
}
