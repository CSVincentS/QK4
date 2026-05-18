#include "kpoddevice.h"
#include "kpodhidworker.h"
#include <QThread>

KpodDevice::KpodDevice(QObject *parent) : QObject(parent) {
    m_hidThread = new QThread(this);
    m_hidThread->setObjectName("KpodHid");
    m_hidWorker = new KpodHidWorker();
    m_hidWorker->moveToThread(m_hidThread);
    connect(m_hidThread, &QThread::started, m_hidWorker, &KpodHidWorker::start);
    connect(m_hidThread, &QThread::finished, m_hidWorker, &QObject::deleteLater);

    // Cache + re-emit worker signals.
    connect(m_hidWorker, &KpodHidWorker::deviceInfoReady, this, [this](KpodDeviceInfo info) {
        m_info = info;
        emit deviceInfoReady();
    });
    connect(m_hidWorker, &KpodHidWorker::deviceArrived, this, [this]() {
        m_polling = true;
        emit deviceConnected();
    });
    connect(m_hidWorker, &KpodHidWorker::deviceRemoved, this, [this]() {
        m_polling = false;
        emit deviceDisconnected();
    });
    connect(m_hidWorker, &KpodHidWorker::encoderRotated, this, &KpodDevice::encoderRotated);
    connect(m_hidWorker, &KpodHidWorker::rockerPositionChanged, this, [this](int position) {
        m_lastRocker = static_cast<RockerPosition>(position);
        emit rockerPositionChanged(m_lastRocker);
    });
    connect(m_hidWorker, &KpodHidWorker::buttonTapped, this, &KpodDevice::buttonTapped);
    connect(m_hidWorker, &KpodHidWorker::buttonHeld, this, &KpodDevice::buttonHeld);
    connect(m_hidWorker, &KpodHidWorker::pollError, this, &KpodDevice::pollError);

    m_hidThread->start();
}

KpodDevice::~KpodDevice() {
    if (m_hidWorker) {
        // Synchronously drain the worker (stop timers, close handle, hid_exit) before
        // tearing the thread down — mirrors KpodPlusDevice's shutdown pattern.
        QMetaObject::invokeMethod(m_hidWorker, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (m_hidThread) {
        m_hidThread->quit();
        m_hidThread->wait(2000);
    }
}

bool KpodDevice::isDetected() const {
    return m_info.detected;
}

KpodDeviceInfo KpodDevice::deviceInfo() const {
    return m_info;
}

bool KpodDevice::isPolling() const {
    return m_polling;
}

KpodDevice::RockerPosition KpodDevice::rockerPosition() const {
    return m_lastRocker;
}

bool KpodDevice::startPolling() {
    if (m_polling)
        return true;
    QMetaObject::invokeMethod(m_hidWorker, "openDevice", Qt::QueuedConnection);
    // Result is asynchronous; isPolling() becomes true on the deviceArrived signal.
    return m_info.detected;
}

void KpodDevice::stopPolling() {
    QMetaObject::invokeMethod(m_hidWorker, "closeDevice", Qt::QueuedConnection);
}
