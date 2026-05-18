#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QThread>
#include <atomic>

class KpodDevice;
class KpodPlusDevice;
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
    KpodPlusDevice *kpodPlusDevice() const { return m_kpodPlusDevice; }
    HalikeyDevice *halikeyDevice() const { return m_halikeyDevice; }
    IambicKeyer *keyer() const { return m_iambicKeyer; }
    SidetoneGenerator *sidetone() const { return m_sidetoneGenerator; }

    // True when KPOD+ keyer is active (suppresses HaliKey → IambicKeyer + Sidetone)
    bool isKpodPlusKeyerActive() const;

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
    void onKpodEncoderRotatedWithRocker(int ticks, int rockerPosition);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // KPOD USB tuning knob
    KpodDevice *m_kpodDevice;

    // KPOD+ USB keyer device (libusb, vendor-specific class)
    KpodPlusDevice *m_kpodPlusDevice;

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

    // V1.4 PTT-line demux destination: the V1.4 firmware multiplexes paddle dit and foot
    // pedal on CTS. The pttStateChanged handler decides where the rising edge goes based
    // on current mode (CW → IambicKeyer dit-paddle; non-CW → MainWindow PTT). The
    // destination is captured at rising edge so that:
    //   - A falling edge dispatches to the same destination that received the rising edge,
    //     not whichever mode happens to be current at release time.
    //   - A mode change while the line is held fires the matching up event to the previous
    //     destination, so neither IambicKeyer nor MainWindow's PTT handler gets stuck in
    //     a "still pressed" state across the transition.
    // compare_exchange_strong on the cleanup path ensures only one thread (mode-change
    // handler OR falling-edge handler) wins, with no double-release.
    enum V14PttDest { V14PttNone = 0, V14PttDitPaddle = 1, V14PttPtt = 2 };
    std::atomic<int> m_v14PttDestination{V14PttNone};
};

#endif // HARDWARECONTROLLER_H
