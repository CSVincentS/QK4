#ifndef HALIKEYDEVICE_H
#define HALIKEYDEVICE_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSerialPortInfo>
#include <QString>
#include <QThread>
#include <atomic>

class HaliKeyWorkerBase;

struct HaliKeyPortInfo {
    QString portName;
};

/**
 * @brief Owner of the HaliKey CW paddle device. Creates a worker (V1.4 / MIDI / Linux TIOCMIWAIT
 *        variant — see HaliKeyWorkerBase) on `m_workerThread` and exposes debounced paddle +
 *        PTT-footswitch signals.
 *
 * Debounce: timestamp-based. Each raw event compares its arrival time against the last
 * accepted edge for that line. Same-direction events (raw == confirmed) are dropped
 * unconditionally. Opposite-direction events are accepted only if the bounce window
 * (DEBOUNCE_NS) has elapsed since the last edge. No QTimer is involved — the entire debounce
 * decision happens inline on whichever worker thread delivered the event. This eliminates
 * any coupling to the main-thread event loop; under heavy GUI load, paddle events still
 * reach the iambic keyer with sub-millisecond latency.
 *
 * Thread note: raw atomics and last-edge timestamps are written from the worker callback
 * thread. Signals are emitted from the same thread that called onRaw*.
 */
class HalikeyDevice : public QObject {
    Q_OBJECT

public:
    explicit HalikeyDevice(QObject *parent = nullptr);
    ~HalikeyDevice();

    // Port management
    bool openPort(const QString &portName);
    void closePort();
    bool isConnected() const;
    QString portName() const;

    // Available ports
    static QStringList availablePorts();
    static QList<HaliKeyPortInfo> availablePortsDetailed();
    static QStringList availableMidiDevices();

    // 3 ms in nanoseconds. Physical contact bounce settles in well under 1 ms; 3 ms is
    // a generous floor that doesn't constrain even ~150 WPM keying (dit = 8 ms at 150 WPM).
    // Public because the inline edge-acceptance helper in the .cpp references it.
    static constexpr qint64 DEBOUNCE_NS = 3'000'000;

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    // Paddle state changes (debounced)
    void ditStateChanged(bool pressed);
    void dahStateChanged(bool pressed);
    void pttStateChanged(bool pressed);

private:
    void onRawDit(bool pressed);
    void onRawDah(bool pressed);
    void onRawPtt(bool pressed);

    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;

    // Confirmed (post-debounce) paddle state — written and read across multiple worker
    // threads (RtMidi callback on MIDI variant, V1.4 monitor thread on serial variant).
    std::atomic<bool> m_confirmedDitState{false};
    std::atomic<bool> m_confirmedDahState{false};
    std::atomic<bool> m_confirmedPttState{false};

    // Monotonic clock for debounce timestamps. Started in the constructor.
    QElapsedTimer m_clock;

    // Last accepted-edge timestamp per line, in nanoseconds since m_clock started.
    // Used to suppress contact-bounce by checking elapsed >= DEBOUNCE_NS before
    // accepting a state transition.
    std::atomic<qint64> m_lastDitEdgeNs{0};
    std::atomic<qint64> m_lastDahEdgeNs{0};
    std::atomic<qint64> m_lastPttEdgeNs{0};
};

#endif // HALIKEYDEVICE_H
