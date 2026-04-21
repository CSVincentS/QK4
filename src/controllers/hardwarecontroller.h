#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QThread>
#include <atomic>

class KpodDevice;
class HalikeyDevice;
class IambicKeyer;
class SidetoneGenerator;
class RadioState;
class ConnectionController;

/**
 * @brief Owns hardware-side workers: KPOD USB knob (main thread), HaliKey CW paddle
 *        (HalikeyDevice — holds its own worker thread), IambicKeyer (HighPriority keyer thread),
 *        SidetoneGenerator (sidetone thread). Translates KPOD button presses → macroRequested
 *        and HaliKey PTT footswitch → pttRequested for MainWindow dispatch.
 */
class HardwareController : public QObject {
    Q_OBJECT

public:
    HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent = nullptr);
    ~HardwareController();

    // Accessors for OptionsDialog integration
    KpodDevice *kpodDevice() const { return m_kpodDevice; }
    HalikeyDevice *halikeyDevice() const { return m_halikeyDevice; }
    IambicKeyer *keyer() const { return m_iambicKeyer; }
    SidetoneGenerator *sidetone() const { return m_sidetoneGenerator; }

    void shutdownSidetone();

signals:
    // KPOD button press → MainWindow dispatches macro
    void macroRequested(const QString &functionId);

    // HaliKey footswitch PTT → MainWindow triggers TX
    void pttRequested(bool active);

private slots:
    void onKpodEncoderRotated(int ticks);
    void onKpodPollError(const QString &error);
    void onKpodEnabledChanged(bool enabled);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // KPOD USB tuning knob
    KpodDevice *m_kpodDevice;

    // HaliKey CW paddle device
    HalikeyDevice *m_halikeyDevice;

    // Iambic keyer state machine (HighPriority thread)
    IambicKeyer *m_iambicKeyer;
    QThread *m_keyerThread = nullptr;

    // Local sidetone generator (dedicated thread)
    SidetoneGenerator *m_sidetoneGenerator;
    QThread *m_sidetoneThread = nullptr;

    // WHY: HaliKey paddle state is delivered via DirectConnection (zero-latency CW keying)
    // from the HaliKey worker thread. That path must know the current operating mode to
    // route dit/PTT correctly, but reading m_radioState->mode() from the worker thread
    // races with parseCATCommand writing it on the main thread. Mirror mode into this
    // atomic from a queued modeChanged connection; the DirectConnection lambda reads it.
    std::atomic<int> m_cachedMode{0}; // initialized to RadioState::Unknown in the .cpp
};

#endif // HARDWARECONTROLLER_H
