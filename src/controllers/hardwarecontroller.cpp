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

    // Helper: apply saved KPOD+ keyer config to both the device and the K4
    auto applyKpodPlusConfig = [this]() {
        auto *settings = RadioSettings::instance();
        int wpm = settings->kpodPlusKeyerSpeed();

        // Configure the KPOD+ device
        m_kpodPlusDevice->setKeyerSpeed(wpm);
        m_kpodPlusDevice->setCwPitch(settings->kpodPlusCwPitch());
        m_kpodPlusDevice->setKeyerParams(settings->kpodPlusIambicMode(), settings->kpodPlusPaddleReversed());
        m_kpodPlusDevice->setEncodeMode(settings->kpodPlusEncodeMode());
        m_kpodPlusDevice->setStuckTimeout(settings->kpodPlusStuckTimeout());

        // Sync element length with K4 so it knows dit/dah duration for KZ commands
        if (m_connectionController->isConnected()) {
            int ditMs = 1200 / wpm;
            m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
        }
    };

    // Auto-start polling on device arrival
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
        }
    });

    // Auto-start on detection at startup
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceInfoReady, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && m_kpodPlusDevice->isDetected() &&
            !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
        }
    });

    // When radio connects while KPOD+ is already polling, send KZL so K4 knows element length
    connect(m_connectionController, &ConnectionController::radioReady, this, [this]() {
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            int wpm = RadioSettings::instance()->kpodPlusKeyerSpeed();
            int ditMs = 1200 / wpm;
            m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
        }
    });

    // Bidirectional sync: KPOD+ settings ↔ K4 radio state
    //
    // When KPOD+ WPM or pitch changes in the UI (via RadioSettings), sync to K4:
    //   - WPM → KS command + KZL element length
    //   - Pitch → KP command (CW pitch in tens of Hz)
    connect(RadioSettings::instance(), &RadioSettings::kpodPlusSettingsChanged, this, [this]() {
        if (!m_kpodPlusDevice || !m_kpodPlusDevice->isPolling())
            return;
        if (!m_connectionController->isConnected())
            return;

        auto *settings = RadioSettings::instance();
        int wpm = settings->kpodPlusKeyerSpeed();
        int pitchHz = settings->kpodPlusCwPitch();

        // Sync WPM → K4 (KS command) + element length (KZL)
        m_connectionController->sendCAT(QString("KS%1;").arg(wpm, 3, 10, QChar('0')));
        int ditMs = 1200 / wpm;
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));

        // Sync pitch → K4 (CW command, value in tens of Hz: e.g. CW55; = 550 Hz)
        int pitchTenHz = pitchHz / 10;
        m_connectionController->sendCAT(QString("CW%1;").arg(pitchTenHz, 2, 10, QChar('0')));

        // Sync iambic mode + paddle orientation → K4 (KP command)
        QChar iambic = settings->kpodPlusIambicMode() == 1 ? QChar('B') : QChar('A');
        QChar paddle = settings->kpodPlusPaddleReversed() ? QChar('R') : QChar('N');
        int weight = m_radioState->keyingWeight();
        if (weight < 90)
            weight = 100; // Default if not yet received from K4
        m_connectionController->sendCAT(QString("KP%1%2%3;").arg(iambic).arg(paddle).arg(weight, 3, 10, QChar('0')));
    });

    // When K4 radio WPM changes (e.g. from front panel), sync to KPOD+ device
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setKeyerSpeed(wpm);
            // Update persisted setting (without re-triggering K4 sync — the change came FROM K4)
            RadioSettings::instance()->blockSignals(true);
            RadioSettings::instance()->setKpodPlusKeyerSpeed(wpm);
            RadioSettings::instance()->blockSignals(false);
        }
    });

    // When K4 radio CW pitch changes, sync to KPOD+ device
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setCwPitch(pitchHz);
            RadioSettings::instance()->blockSignals(true);
            RadioSettings::instance()->setKpodPlusCwPitch(pitchHz);
            RadioSettings::instance()->blockSignals(false);
        }
    });

    // EP02 keyer data → raw passthrough to K4 tunnel
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after
    // the last ';'. Forward the trimmed buffer as a single sendCAT() call — one protocol
    // packet, one TCP write, one flush. The K4 parses on ';' delimiters regardless of
    // framing. This preserves the device's native batching at high WPM and avoids per-
    // element string allocation + multiple TCP writes.
    connect(m_kpodPlusDevice, &KpodPlusDevice::keyerDataReceived, this, [this](const QByteArray &data) {
        // Strip trailing null bytes from the 32-byte buffer
        int len = data.size();
        while (len > 0 && data.at(len - 1) == '\0')
            --len;
        if (len > 0) {
            m_connectionController->sendCAT(QString::fromLatin1(data.constData(), len));
        }
    });

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
    // HaliKey CW paddle device
    // =========================================================================
    m_halikeyDevice = new HalikeyDevice(this);

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

    // Update keyer mode/reversal when KP settings change (from K4 front panel or TX button row)
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        QMetaObject::invokeMethod(
            m_iambicKeyer, "setMode", Qt::QueuedConnection,
            Q_ARG(IambicKeyer::Mode, iambic == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
        QMetaObject::invokeMethod(m_iambicKeyer, "setReversed", Qt::QueuedConnection, Q_ARG(bool, paddle == 'R'));

        // Sync to KPOD+ device (if active)
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            int mode = (iambic == 'B') ? 1 : 0;
            bool reversed = (paddle == 'R');
            m_kpodPlusDevice->setKeyerParams(mode, reversed);
            RadioSettings::instance()->blockSignals(true);
            RadioSettings::instance()->setKpodPlusIambicMode(mode);
            RadioSettings::instance()->setKpodPlusPaddleReversed(reversed);
            RadioSettings::instance()->blockSignals(false);
        }
    });

    // =========================================================================
    // Keyer → CAT commands + sidetone audio
    // =========================================================================

    // Keyer element started — send KZ command to I/O thread + sidetone to sidetone thread
    // Both are suppressed when KPOD+ keyer is active (device handles keying + sidetone)
    connect(m_iambicKeyer, &IambicKeyer::elementStarted, this, [this](bool isDit) {
        if (isKpodPlusKeyerActive())
            return;
        m_connectionController->sendCAT(isDit ? "KZ.;" : "KZ-;");
    });
    connect(m_iambicKeyer, &IambicKeyer::elementStarted, m_sidetoneGenerator,
            [this, sg = m_sidetoneGenerator](bool isDit) {
                if (isKpodPlusKeyerActive())
                    return;
                isDit ? sg->playSingleDit() : sg->playSingleDah();
            });

    // Keyer finished — stop local sidetone (K4 unkeys itself after each KZ element)
    connect(m_iambicKeyer, &IambicKeyer::keyingFinished, m_sidetoneGenerator, [this, sg = m_sidetoneGenerator]() {
        if (isKpodPlusKeyerActive())
            return;
        sg->stopElement();
    });

    // Character boundary — keyer went idle between elements
    connect(m_iambicKeyer, &IambicKeyer::characterSpace, this, [this]() { m_connectionController->sendCAT("KZ ;"); });

    // Restart after pause — send KZP with elapsed ms before next element
    connect(m_iambicKeyer, &IambicKeyer::restartAfterPause, this,
            [this](int ms) { m_connectionController->sendCAT(QString("KZP%1;").arg(ms, 4, 10, QChar('0'))); });

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
    // thread — data race. The atomic cache below is updated from a queued modeChanged.
    m_cachedMode.store(static_cast<int>(m_radioState->mode()), std::memory_order_relaxed);
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_cachedMode.store(static_cast<int>(mode), std::memory_order_relaxed); });

    connect(
        m_halikeyDevice, &HalikeyDevice::ditStateChanged, this,
        [this](bool pressed) {
            // Suppress HaliKey dit when KPOD+ keyer owns the CW path
            if (isKpodPlusKeyerActive())
                return;
            auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_relaxed));
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
            auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_relaxed));
            const bool inCw = (mode == RadioState::CW || mode == RadioState::CW_R);
            const bool isV14 = (RadioSettings::instance()->halikeyDeviceType() != 1);
            if (inCw && isV14) {
                m_iambicKeyer->setDitPaddle(active);
            } else if (!inCw) {
                emit pttRequested(active);
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
    return m_kpodPlusDevice && m_kpodPlusDevice->isDetected() && m_kpodPlusDevice->isPolling();
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
