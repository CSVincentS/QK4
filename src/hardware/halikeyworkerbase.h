#ifndef HALIKEYWORKERBASE_H
#define HALIKEYWORKERBASE_H

#include <QObject>
#include <QString>
#include <atomic>

/**
 * @brief Base class for HaliKey paddle-input workers. Concrete workers (one per platform) live on
 *        `HalikeyDevice::m_workerThread` and translate hardware events into
 *        `ditStateChanged` / `dahStateChanged` / `pttStateChanged` signals.
 *
 * WHY three concrete subclasses instead of one:
 *   - `HaliKeyV14Worker`  — native V1.4 hardware protocol (direct serial frames).
 *   - `HaliKeyMidiWorker` — MIDI interface variant (used when the V1.4 firmware exposes MIDI).
 *   - (Linux-only)        — `TIOCMIWAIT` blocking ioctl on the serial FD.
 * Each has a different event model (line-oriented reads, MIDI messages, blocking ioctl) so sharing
 * a single loop is impossible.
 *
 * WHY `prepareShutdown()` exists: the Linux `TIOCMIWAIT`-based worker blocks inside a kernel
 * ioctl and cannot observe `m_running = false` until an edge arrives. `prepareShutdown()` is the
 * hook that variant uses (typically toggling a modem-control line on the same FD) to force the
 * ioctl to return so the thread can exit cleanly. The base no-op is correct for variants whose
 * event loops poll `m_running` naturally.
 */
class HaliKeyWorkerBase : public QObject {
    Q_OBJECT

public:
    explicit HaliKeyWorkerBase(const QString &portName, QObject *parent = nullptr);
    ~HaliKeyWorkerBase() override = default;

    virtual void prepareShutdown() {} // Override for platform-specific unblocking (e.g. Linux TIOCMIWAIT)

public slots:
    virtual void start() = 0; // Called when thread starts
    void stop();              // Sets atomic flag to exit loop

signals:
    void ditStateChanged(bool pressed);
    void dahStateChanged(bool pressed);
    void pttStateChanged(bool pressed);
    void errorOccurred(const QString &error);
    void portOpened();

protected:
    QString m_portName;
    std::atomic<bool> m_running{false};
};

#endif // HALIKEYWORKERBASE_H
