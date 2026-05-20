#include "hardwarecontroller.h"
#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "models/radiostate.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qk4Hardware, "qk4.hardware")

HardwareController::HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connectionController(connController) {
    // =========================================================================
    // KPOD USB tuning knob
    // =========================================================================
    m_kpodDevice = new KpodDevice(this);

    connect(m_kpodDevice, &KpodDevice::encoderRotated, this, &HardwareController::onKpodEncoderRotated);
    connect(m_kpodDevice, &KpodDevice::pollError, this, &HardwareController::onKpodPollError);

    // KPOD button signals → macro execution via MainWindow
    connect(m_kpodDevice, &KpodDevice::buttonTapped, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1T").arg(buttonNum)); });
    connect(m_kpodDevice, &KpodDevice::buttonHeld, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1H").arg(buttonNum)); });

    // Auto-start polling when device arrives
    connect(m_kpodDevice, &KpodDevice::deviceConnected, this, [this]() {
        if (RadioSettings::instance()->kpodEnabled() && !m_kpodDevice->isPolling()) {
            m_kpodDevice->startPolling();
        }
    });

    // Settings: KPOD enable/disable
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this,
            &HardwareController::onKpodEnabledChanged);

    // WHY: KpodDevice::detectDevice() is deferred to the first event-loop tick (see
    // kpoddevice.cpp constructor note) to keep the 400ms hid_open_path retry off the
    // main thread at startup. isDetected() is false at this point; the deviceInfoReady
    // signal fires after detection completes and is the correct point to auto-start
    // polling if the user had KPOD enabled.
    connect(m_kpodDevice, &KpodDevice::deviceInfoReady, this, [this]() {
        if (RadioSettings::instance()->kpodEnabled() && m_kpodDevice->isDetected() && !m_kpodDevice->isPolling()) {
            m_kpodDevice->startPolling();
        }
    });

    // =========================================================================
    // KPOD+ USB keyer device (vendor-specific class, libusb)
    // =========================================================================
    m_kpodPlusDevice = new KpodPlusDevice(this);

    // Encoder/button/rocker signals — KPOD+ encoder reads its own rocker position
    connect(m_kpodPlusDevice, &KpodPlusDevice::encoderRotated, this, [this](int ticks) {
        if (!m_connectionController->isConnected())
            return;
        // Use KPOD+ rocker position (same encoding as KPOD)
        int rocker = static_cast<int>(m_kpodPlusDevice->rockerPosition());
        onKpodEncoderRotatedWithRocker(ticks, rocker);
    });
    connect(m_kpodPlusDevice, &KpodPlusDevice::pollError, this, &HardwareController::onKpodPollError);
    connect(m_kpodPlusDevice, &KpodPlusDevice::buttonTapped, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1T").arg(buttonNum)); });
    connect(m_kpodPlusDevice, &KpodPlusDevice::buttonHeld, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1H").arg(buttonNum)); });

    // At KPOD+ plug-in, push the user's saved KPOD+ settings (from
    // RadioSettings) down to the device. KPOD+ keyer state is intentionally
    // independent of the K4/QK4 keyer state — RadioState is not consulted
    // and the K4 is not informed of the KPOD+'s settings. User-driven
    // changes on the KPOD+ settings page push to the device directly
    // (see KpodPage handlers).
    auto applyKpodPlusConfig = [this]() {
        auto *settings = RadioSettings::instance();
        m_kpodPlusDevice->setKeyerSpeed(settings->kpodPlusKeyerSpeed());
        m_kpodPlusDevice->setCwPitch(settings->kpodPlusCwPitch());
        m_kpodPlusDevice->setKeyerParams(settings->kpodPlusIambicMode(), settings->kpodPlusPaddleReversed());
        m_kpodPlusDevice->setEncodeMode(settings->kpodPlusEncodeMode());
        m_kpodPlusDevice->setStuckTimeout(settings->kpodPlusStuckTimeout());
    };

    // Auto-start polling on device arrival
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
        }
        // KPOD+ now owns the keyer; the local iambic path drops its KZ output.
        m_connectionController->setKpodPlusKeyerActive(true);
    });

    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceDisconnected, this,
            [this]() { m_connectionController->setKpodPlusKeyerActive(false); });

    // Auto-start on detection at startup
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceInfoReady, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && m_kpodPlusDevice->isDetected() &&
            !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
            // Pre-emptively claim the keyer path the moment KPOD+ is detected
            // (~10-100 ms before deviceConnected fires). Otherwise any paddle
            // events during the openDevice window slip through to the local
            // iambic + sidetone, which the device itself handles. Released by
            // the deviceDisconnected handler or when the user disables KPOD+
            // in settings.
            m_connectionController->setKpodPlusKeyerActive(true);
        }
    });

    // EP02 keyer data → straight to the I/O thread.
    //
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after the
    // last ';'. By targeting TcpClient as the receiver (lives on the I/O thread) with a
    // queued connection we skip the main thread entirely on this hot path. Earlier the
    // KpodPlusEP02 thread emitted into a lambda on the main thread, which then marshalled
    // again into TcpClient on the I/O thread — two hops, sensitive to GUI load. sendCATBytes
    // trims NUL padding on the I/O thread and hands off to sendCAT().
    connect(m_kpodPlusDevice, &KpodPlusDevice::keyerDataReceived, m_connectionController->tcpClient(),
            &TcpClient::sendCATBytes, Qt::QueuedConnection);

    // KPOD enable/disable also controls KPOD+
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            if (m_kpodPlusDevice->isDetected() && !m_kpodPlusDevice->isPolling()) {
                m_kpodPlusDevice->startPolling();
            }
        } else {
            m_kpodPlusDevice->stopPolling();
        }
    });

    // =========================================================================
    // HaliKey CW paddle device — device type injected here so HalikeyDevice
    // itself doesn't reach into RadioSettings (Phase 3 layering cleanup).
    // =========================================================================
    m_halikeyDevice = new HalikeyDevice(RadioSettings::instance()->halikeyDeviceType(), this);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, m_halikeyDevice,
            &HalikeyDevice::setDeviceType);

    // =========================================================================
    // Sidetone generator (dedicated thread for low-latency audio feedback)
    // MUST be created BEFORE IambicKeyer signal connections that use it
    // =========================================================================
    m_sidetoneGenerator = new SidetoneGenerator(nullptr);
    m_sidetoneThread = new QThread(this);
    m_sidetoneThread->setObjectName("Sidetone");
    m_sidetoneGenerator->moveToThread(m_sidetoneThread);
    m_sidetoneThread->start();
    QMetaObject::invokeMethod(m_sidetoneGenerator, "start", Qt::QueuedConnection);

    // Set sidetone to same output device as AudioEngine
    QString savedSidetoneDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSidetoneDevice.isEmpty()) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, savedSidetoneDevice));
    }

    // Follow speaker device changes at runtime
    connect(RadioSettings::instance(), &RadioSettings::speakerDeviceChanged, this, [this](const QString &deviceId) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, deviceId));
    });

    // Set initial sidetone frequency from radio state if available
    if (m_radioState->cwPitch() > 0) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->cwPitch()));
    }

    // Update sidetone frequency when CW pitch changes
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setFrequency", Qt::QueuedConnection, Q_ARG(int, pitchHz));
    });

    // Set initial sidetone volume from RadioSettings (independent of K4's MON level)
    QMetaObject::invokeMethod(m_sidetoneGenerator, "setVolume", Qt::QueuedConnection,
                              Q_ARG(float, RadioSettings::instance()->sidetoneVolume() / 100.0f));

    // Update sidetone volume when changed in Options
    connect(RadioSettings::instance(), &RadioSettings::sidetoneVolumeChanged, this, [this](int value) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setVolume", Qt::QueuedConnection, Q_ARG(float, value / 100.0f));
    });

    // Set initial keyer speed from radio state if available
    if (m_radioState->keyerSpeed() > 0) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setKeyerSpeed", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->keyerSpeed()));
    }

    // =========================================================================
    // Iambic keyer state machine (HighPriority thread for CW element timing)
    // =========================================================================
    m_iambicKeyer = new IambicKeyer(nullptr);
    m_keyerThread = new QThread(this);
    m_keyerThread->setObjectName("Keyer");
    m_iambicKeyer->moveToThread(m_keyerThread);
    m_keyerThread->start(QThread::HighPriority);

    // Initialize keyer from RadioState KP settings
    int initWpm = m_radioState->keyerSpeed();
    if (initWpm <= 0)
        initWpm = 20;
    QMetaObject::invokeMethod(m_iambicKeyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, initWpm));
    QMetaObject::invokeMethod(
        m_iambicKeyer, "setMode", Qt::QueuedConnection,
        Q_ARG(IambicKeyer::Mode, m_radioState->iambicMode() == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
    QMetaObject::invokeMethod(m_iambicKeyer, "setReversed", Qt::QueuedConnection,
                              Q_ARG(bool, m_radioState->paddleOrientation() == 'R'));

    // Update sidetone and keyer speed when WPM changes
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        // WHY use invokeMethod instead of a direct call: SidetoneGenerator lives on
        // m_sidetoneThread. setKeyerSpeed only writes a std::atomic<int> today (safe direct),
        // but the matching invokeMethod for m_iambicKeyer on the next line establishes the
        // cross-thread pattern — future changes to setKeyerSpeed that touch non-atomic
        // members would otherwise introduce a silent race with no call-site warning.
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setKeyerSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        QMetaObject::invokeMethod(m_iambicKeyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        // Sync element length with K4 server
        int ditMs = 1200 / wpm;
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
    });

    // Update local iambic keyer mode/reversal when K4 KP settings change.
    // KPOD+ keyer state is intentionally NOT updated from this signal —
    // KPOD+ keyer params live independently of the K4's KP settings.
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        QMetaObject::invokeMethod(
            m_iambicKeyer, "setMode", Qt::QueuedConnection,
            Q_ARG(IambicKeyer::Mode, iambic == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
        QMetaObject::invokeMethod(m_iambicKeyer, "setReversed", Qt::QueuedConnection, Q_ARG(bool, paddle == 'R'));
    });

    // =========================================================================
    // Keyer → CAT commands + sidetone audio
    // =========================================================================
    //
    // Wire keyer signals (emitted on the HighPriority keyer thread) directly to TcpClient on
    // the I/O thread via queued connections. The main thread is not on this hot path.
    // The atomic gate on ConnectionController drops emissions when the KPOD+ device owns
    // the keyer; the local-iambic state machine still runs but its KZ output is suppressed.
    //
    // Order preservation: all three signals (restartAfterPause, elementStarted, characterSpace)
    // originate on the same source thread (keyer) and target the same destination thread (I/O),
    // so Qt's event queue keeps them FIFO. The on-air ordering is:
    //   restartAfterPause → elementStarted (per enterElement)
    //   characterSpace → keyingFinished     (per goIdle)
    // matching the K4 KZ protocol (KZP timing → KZ./KZ- elements → KZ ; letter marker —
    // literal 0x20 SPACE, confirmed by hexdump of live KPOD+ EP02 traffic; the PDF spec's
    // monospace rendering of "KZ_;" is a typographic artifact, not an underscore byte).
    auto *tc = m_connectionController->tcpClient();
    auto *cc = m_connectionController;
    connect(
        m_iambicKeyer, &IambicKeyer::elementStarted, tc,
        [tc, cc](bool isDit) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(isDit ? QStringLiteral("KZ.;") : QStringLiteral("KZ-;"));
        },
        Qt::QueuedConnection);
    connect(
        m_iambicKeyer, &IambicKeyer::characterSpace, tc,
        [tc, cc]() {
            if (cc->isKpodPlusKeyerActive())
                return;
            // WHY space, not underscore: the Elecraft KPodKeyerInterface.pdf renders the
            // letter-space marker as "KZ_;" but a hexdump of EP02 traffic from a live KPOD+
            // device shows the actual byte is 0x20 (literal SPACE). The K4 firmware accepts
            // that form; emitting "KZ_;" with a real underscore is what the parser rejects.
            // PDF rendering artifact — the underline beneath the space in the spec's
            // monospace font reads as an underscore.
            tc->sendCAT(QStringLiteral("KZ ;"));
        },
        Qt::QueuedConnection);
    connect(
        m_iambicKeyer, &IambicKeyer::restartAfterPause, tc,
        [tc, cc](int ms) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(QStringLiteral("KZP%1;").arg(ms, 4, 10, QChar('0')));
        },
        Qt::QueuedConnection);

    // Sidetone stays on its own thread. Sidetone gate uses the local helper so a hot KPOD+
    // takeover silences feedback immediately even before the next emit lands on I/O.
    connect(m_iambicKeyer, &IambicKeyer::elementStarted, m_sidetoneGenerator,
            [this, sg = m_sidetoneGenerator](bool isDit) {
                if (isKpodPlusKeyerActive())
                    return;
                isDit ? sg->playSingleDit() : sg->playSingleDah();
            });
    connect(m_iambicKeyer, &IambicKeyer::keyingFinished, m_sidetoneGenerator, [this, sg = m_sidetoneGenerator]() {
        if (isKpodPlusKeyerActive())
            return;
        sg->stopElement();
    });

    // =========================================================================
    // HaliKey paddle → keyer (ZERO-LATENCY DirectConnection)
    // =========================================================================
    // HaliKey MIDI sends note 20 (dit) + note 31 (PTT) together on every Tip-to-Sleeve closure.
    // In CW mode: forward dit to keyer, ignore PTT (TX handled by KZ commands).
    // In voice mode: forward PTT to MainWindow, suppress dit (no keying in SSB/AM/FM).
    //
    // WHY read m_cachedMode instead of m_radioState->mode(): this lambda runs on the HaliKey
    // worker thread (DirectConnection from halikeydevice.cpp). m_radioState->mode() reads a
    // non-atomic subsystem field concurrently with parseCATCommand()'s writes on the main
    // thread — data race. The atomic cache below is updated from modeChanged via
    // AutoConnection — both this controller and RadioState live on the main thread, so
    // AutoConnection resolves to DirectConnection and the store runs synchronously on the
    // main thread alongside parseCATCommand. The HaliKey worker thread reads with acquire
    // ordering, paired with the release store here.
    m_cachedMode.store(static_cast<int>(m_radioState->mode()), std::memory_order_release);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        m_cachedMode.store(static_cast<int>(mode), std::memory_order_release);
        // V1.4 mode-transition cleanup: if a paddle/PTT was rising-edge-captured before the
        // transition, fire the matching up event to the OLD destination so neither the
        // IambicKeyer nor MainWindow gets stuck in a half-pressed state. CAS ensures the
        // falling-edge handler doesn't also clean up (whichever fires first wins).
        int dest = m_v14PttDestination.load(std::memory_order_acquire);
        if (dest != V14PttNone) {
            if (m_v14PttDestination.compare_exchange_strong(dest, V14PttNone, std::memory_order_acq_rel)) {
                if (dest == V14PttDitPaddle) {
                    m_iambicKeyer->setDitPaddle(false);
                } else if (dest == V14PttPtt) {
                    emit pttRequested(false);
                }
            }
        }
    });

    connect(
        m_halikeyDevice, &HalikeyDevice::ditStateChanged, this,
        [this](bool pressed) {
            // Suppress HaliKey dit when KPOD+ keyer owns the CW path
            if (isKpodPlusKeyerActive())
                return;
            auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
            if (mode == RadioState::CW || mode == RadioState::CW_R) {
                m_iambicKeyer->setDitPaddle(pressed);
            }
            // In voice/data modes, dit is suppressed — PTT signal handles TX
        },
        Qt::DirectConnection);
    connect(
        m_halikeyDevice, &HalikeyDevice::dahStateChanged, this,
        [this](bool pressed) {
            if (isKpodPlusKeyerActive())
                return;
            m_iambicKeyer->setDahPaddle(pressed);
        },
        Qt::DirectConnection);

    // HaliKey PTT → MainWindow (voice/data modes) or paddle dit (CW mode, V1.4 only).
    // WHY: V1.4 serial firmware can't distinguish foot pedal from paddle dit lever — both
    // drive CTS. We demux by mode here: in CW the CTS edge is treated as the dit-paddle
    // press, in voice it's the foot pedal → PTT. The MIDI variant has a distinct note for
    // the pedal so its CW behavior stays mode-gated to silence (no spurious dit injection).
    connect(
        m_halikeyDevice, &HalikeyDevice::pttStateChanged, this,
        [this](bool active) {
            const bool isV14 = (RadioSettings::instance()->halikeyDeviceType() != 1);
            if (active) {
                // RISING EDGE: pick a destination based on current mode and remember it,
                // so the falling edge (or a mid-press mode change) can fire the matching
                // up event to the SAME destination — even if the mode flipped meanwhile.
                auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
                const bool inCw = (mode == RadioState::CW || mode == RadioState::CW_R);
                if (inCw && isV14) {
                    // KPOD+ owns the keyer? Drop and don't capture a destination — the
                    // matching falling edge will see V14PttNone and also drop.
                    if (isKpodPlusKeyerActive())
                        return;
                    m_v14PttDestination.store(V14PttDitPaddle, std::memory_order_release);
                    m_iambicKeyer->setDitPaddle(true);
                } else if (!inCw) {
                    m_v14PttDestination.store(V14PttPtt, std::memory_order_release);
                    emit pttRequested(true);
                }
                // (MIDI variant in CW falls through silently — its dit comes via note 20,
                // not via the PTT line, so a PTT rising edge here is the foot pedal which
                // shouldn't key in CW.)
            } else {
                // FALLING EDGE: dispatch to whatever destination captured the rising edge.
                // CAS ensures the mode-change cleanup handler doesn't also fire — only one
                // of (mode-change, falling-edge) wins, and the other sees V14PttNone.
                int dest = m_v14PttDestination.load(std::memory_order_acquire);
                if (dest == V14PttNone)
                    return;
                if (m_v14PttDestination.compare_exchange_strong(dest, V14PttNone, std::memory_order_acq_rel)) {
                    if (dest == V14PttDitPaddle) {
                        m_iambicKeyer->setDitPaddle(false);
                    } else if (dest == V14PttPtt) {
                        emit pttRequested(false);
                    }
                }
            }
        },
        Qt::DirectConnection);

    // Enable keyer when radio connects, disable on disconnect
    connect(m_connectionController, &ConnectionController::radioReady, this, [this]() {
        QMetaObject::invokeMethod(m_iambicKeyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, true));
    });
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    QMetaObject::invokeMethod(m_iambicKeyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
                }
            });

    // Stop keyer when HaliKey disconnects (prevents runaway keying
    // if paddle was held when disconnected — Note Off never arrives)
    connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, [this]() {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stopElement", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_iambicKeyer, "stop", Qt::QueuedConnection);
    });

    // Surface HaliKey port-open failures to the user via NotificationWidget. Without
    // this connect, openPort() failures (nonexistent serial port, busy MIDI device,
    // permission denied) were silently swallowed.
    connect(m_halikeyDevice, &HalikeyDevice::connectionError, this,
            [this](const QString &error) { emit hardwareError(QStringLiteral("HaliKey: %1").arg(error)); });
}

void HardwareController::shutdownSidetone() {
    // Synchronous sidetone sink teardown — required from MainWindow::closeEvent
    // so QAudioSink is destroyed while the event loop is still alive, not
    // during libc atexit (which races PipeWire's RT worker on Linux).
    if (m_sidetoneGenerator && m_sidetoneThread && m_sidetoneThread->isRunning()) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stop", Qt::BlockingQueuedConnection);
    }
}

bool HardwareController::isKpodPlusKeyerActive() const {
    // WHY read the atomic gate rather than m_kpodPlusDevice->isPolling():
    // (1) consistency — the I/O-thread iambic CAT lambdas read the same
    // atomic, so a single source of truth across both paths;
    // (2) thread safety — this method is called from the HaliKey worker
    // thread (DirectConnection in the paddle handlers) and reading
    // KpodPlusDevice::isPolling() races with the main-thread setter;
    // (3) timing — the atomic is set on deviceInfoReady (KPOD+ detected),
    // not deviceConnected (KPOD+ open succeeded), so the ~10-100 ms open
    // window doesn't leak paddle events to the local sidetone path.
    return m_connectionController->isKpodPlusKeyerActive();
}

HardwareController::~HardwareController() {
    disconnect(this);
    // Shutdown order: HaliKey → Keyer → Sidetone → KPOD/KPOD+
    // HaliKey stops paddle events first, then keyer (producer of KZ commands) stops
    // before sidetone (the audio consumer) is torn down.

    if (m_halikeyDevice) {
        m_halikeyDevice->closePort();
    }

    if (m_keyerThread) {
        QMetaObject::invokeMethod(m_iambicKeyer, "stop", Qt::BlockingQueuedConnection);
        m_keyerThread->quit();
        m_keyerThread->wait(2000);
    }
    delete m_iambicKeyer; // No parent, must delete manually

    if (m_sidetoneThread) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stop", Qt::BlockingQueuedConnection);
        m_sidetoneThread->quit();
        m_sidetoneThread->wait(2000);
    }
    delete m_sidetoneGenerator; // No parent, must delete manually

    if (m_kpodPlusDevice) {
        m_kpodPlusDevice->stopPolling();
    }

    if (m_kpodDevice) {
        m_kpodDevice->stopPolling();
    }
}

// =============================================================================
// KPOD Event Handlers
// =============================================================================

void HardwareController::onKpodEncoderRotated(int ticks) {
    if (!m_connectionController->isConnected()) {
        return;
    }
    onKpodEncoderRotatedWithRocker(ticks, static_cast<int>(m_kpodDevice->rockerPosition()));
}

void HardwareController::onKpodEncoderRotatedWithRocker(int ticks, int rockerPos) {
    // Action depends on rocker position (shared by KPOD and KPOD+)
    switch (rockerPos) {
    case 2: // RockerLeft — VFO A
    {
        if (m_radioState->lockA())
            break;
        quint64 currentFreq = m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case 0: // RockerCenter — VFO B
    {
        if (m_radioState->lockB())
            break;
        quint64 currentFreq = m_radioState->vfoB();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case 1: // RockerRight — RIT/XIT
        // K4 routes RU;/RD; based on active mode: RIT → RO (VFO A), XIT → RO$ (VFO B)
        // BSET + RIT: use RU$/RD$ to force VFO B's RIT offset
        {
            bool bSet = m_radioState->bSetEnabled();
            bool adjustB = bSet && !m_radioState->xitEnabled();
            QString cmd = (ticks > 0) ? (adjustB ? "RU$;" : "RU;") : (adjustB ? "RD$;" : "RD;");
            int count = qAbs(ticks);
            for (int i = 0; i < count; i++) {
                m_connectionController->sendCAT(cmd);
            }
        }
        break;
    }
}

void HardwareController::onKpodPollError(const QString &error) {
    qCWarning(qk4Hardware) << "KPOD error:" << error;
}

void HardwareController::onKpodEnabledChanged(bool enabled) {
    if (enabled) {
        if (m_kpodDevice->isDetected()) {
            m_kpodDevice->startPolling();
        }
    } else {
        m_kpodDevice->stopPolling();
    }
}
