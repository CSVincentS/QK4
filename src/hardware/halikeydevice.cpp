#include "halikeydevice.h"
#include "halikeymidiworker.h"
#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
#include <QDebug>
#include <RtMidi.h>

Q_LOGGING_CATEGORY(hwHalikey, "hw.halikey")

namespace {
// Inline debounce decision shared by all three lines. Returns true if the event should be
// emitted; false if it's redundant (same state) or a bounce (within DEBOUNCE_NS).
// On accept, updates the confirmed-state atomic and the last-edge timestamp.
bool acceptEdge(bool raw, std::atomic<bool> &confirmed, std::atomic<qint64> &lastEdgeNs, qint64 nowNs) {
    if (raw == confirmed.load(std::memory_order_acquire))
        return false; // Redundant — same direction as last confirmed edge
    const qint64 last = lastEdgeNs.load(std::memory_order_relaxed);
    if (nowNs - last < HalikeyDevice::DEBOUNCE_NS)
        return false; // Bounce — within the dead window
    lastEdgeNs.store(nowNs, std::memory_order_relaxed);
    confirmed.store(raw, std::memory_order_release);
    return true;
}
} // namespace

HalikeyDevice::HalikeyDevice(int deviceType, QObject *parent) : QObject(parent), m_deviceType(deviceType) {
    // Monotonic clock for debounce timestamping. Starting here means nowNs() == 0 represents
    // "construction time" — any real paddle event will have a positive timestamp, so the
    // first edge ever delivered will pass the bounce gate (0 - 0 < DEBOUNCE_NS) only on
    // construction, which is fine because there's no prior confirmed state to compare against.
    m_clock.start();
}

HalikeyDevice::~HalikeyDevice() {
    closePort();
}

void HalikeyDevice::onRawDit(bool pressed) {
    if (acceptEdge(pressed, m_confirmedDitState, m_lastDitEdgeNs, m_clock.nsecsElapsed()))
        emit ditStateChanged(pressed);
}

void HalikeyDevice::onRawDah(bool pressed) {
    if (acceptEdge(pressed, m_confirmedDahState, m_lastDahEdgeNs, m_clock.nsecsElapsed()))
        emit dahStateChanged(pressed);
}

void HalikeyDevice::onRawPtt(bool pressed) {
    if (acceptEdge(pressed, m_confirmedPttState, m_lastPttEdgeNs, m_clock.nsecsElapsed()))
        emit pttStateChanged(pressed);
}

bool HalikeyDevice::openPort(const QString &portName) {
    if (m_connected) {
        closePort();
    }

    m_portName = portName;
    m_confirmedDitState.store(false, std::memory_order_relaxed);
    m_confirmedDahState.store(false, std::memory_order_relaxed);
    m_confirmedPttState.store(false, std::memory_order_relaxed);
    m_lastDitEdgeNs.store(0, std::memory_order_relaxed);
    m_lastDahEdgeNs.store(0, std::memory_order_relaxed);
    m_lastPttEdgeNs.store(0, std::memory_order_relaxed);

    // Create worker based on injected device type (0 = V1.4, 1 = MIDI)
    if (m_deviceType == 1) {
        m_worker = new HaliKeyMidiWorker(portName);
    } else {
        m_worker = new HaliKeyV14Worker(portName);
    }

    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // DirectConnection: onRaw* runs on whatever worker thread delivered the event (RtMidi
    // callback for MIDI, monitor thread for V1.4). Both update the same atomics + emit
    // ditStateChanged from that thread. Downstream connections are independently safe
    // (HardwareController's dit/dah handlers only touch atomics and queue to the keyer thread).
    connect(m_worker, &HaliKeyWorkerBase::ditStateChanged, this, &HalikeyDevice::onRawDit, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::dahStateChanged, this, &HalikeyDevice::onRawDah, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::pttStateChanged, this, &HalikeyDevice::onRawPtt, Qt::DirectConnection);
    connect(m_worker, &HaliKeyWorkerBase::portOpened, this, [this]() {
        m_connected = true;
        emit connected();
    });
    connect(m_worker, &HaliKeyWorkerBase::errorOccurred, this, [this](const QString &error) {
        qCWarning(hwHalikey) << "HalikeyDevice: Worker error -" << error;
        closePort();
        emit connectionError(error);
    });

    // Start worker when thread starts
    connect(m_workerThread, &QThread::started, m_worker, &HaliKeyWorkerBase::start);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
    return true;
}

void HalikeyDevice::closePort() {
    // Disconnect worker signals FIRST — prevent stale queued events
    // from reaching slots after state reset
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->stop();
        m_worker->prepareShutdown();
    }

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_worker = nullptr; // Deleted by QThread::finished -> deleteLater

    bool wasConnected = m_connected;
    m_connected = false;
    m_confirmedDitState.store(false, std::memory_order_relaxed);
    m_confirmedDahState.store(false, std::memory_order_relaxed);
    m_confirmedPttState.store(false, std::memory_order_relaxed);
    m_lastDitEdgeNs.store(0, std::memory_order_relaxed);
    m_lastDahEdgeNs.store(0, std::memory_order_relaxed);
    m_lastPttEdgeNs.store(0, std::memory_order_relaxed);

    if (wasConnected) {
        emit disconnected();
    }
}

bool HalikeyDevice::isConnected() const {
    return m_connected;
}

QString HalikeyDevice::portName() const {
    return m_portName;
}

QStringList HalikeyDevice::availablePorts() {
    QStringList ports;
    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portInfos) {
        ports.append(info.portName());
    }
    return ports;
}

QList<HaliKeyPortInfo> HalikeyDevice::availablePortsDetailed() {
    QList<HaliKeyPortInfo> ports;
    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portInfos) {
        HaliKeyPortInfo pi;
        pi.portName = info.portName();
        ports.append(pi);
    }
    return ports;
}

QStringList HalikeyDevice::availableMidiDevices() {
    // System virtual MIDI devices to exclude from the list
    static const QStringList excludedPrefixes = {"IAC Driver"};

    QStringList devices;
    try {
        RtMidiIn midi;
        unsigned int portCount = midi.getPortCount();
        for (unsigned int i = 0; i < portCount; i++) {
            QString name = QString::fromStdString(midi.getPortName(i));
            bool excluded = false;
            for (const QString &prefix : excludedPrefixes) {
                if (name.startsWith(prefix, Qt::CaseInsensitive)) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                devices.append(name);
            }
        }
    } catch (RtMidiError &error) {
        qCWarning(hwHalikey) << "HalikeyDevice: MIDI enumeration failed:" << QString::fromStdString(error.getMessage());
    }
    return devices;
}
