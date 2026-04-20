#include "mainwindow.h"
#include "utils/radioutils.h"
#include "ui/radiomanagerdialog.h"
#include "ui/sidecontrolpanel.h"
#include "ui/rightsidepanel.h"
#include "ui/bottommenubar.h"
#include "controllers/featuremenucontroller.h"
#include "controllers/modepopupcontroller.h"
#include "ui/menuoverlay.h"
#include "controllers/bandnavigationcontroller.h"
#include "controllers/buttonrowdispatcher.h"
#include "controllers/macrocontroller.h"
#include "controllers/processingdisplaycontroller.h"
#include "controllers/popupmanager.h"
#include "models/macroids.h"
#include "ui/optionsdialog.h"
#include "ui/notificationwidget.h"
#include "ui/vforowwidget.h"
#include "ui/filterindicatorwidget.h"
#include "ui/k4styles.h"
#include "controllers/antennaconfigcontroller.h"
#include "controllers/textdecodecontroller.h"
#include "controllers/menucontroller.h"
#include "controllers/dxclustercontroller.h"
#include "controllers/spectrumcontroller.h"
#include "dsp/panadapter_rhi.h"
#include "dsp/minipan_rhi.h"
#include "ui/frequencydisplaywidget.h"
#include "controllers/audiocontroller.h"
#include "controllers/hardwarecontroller.h"
#include "controllers/kpa1500uicontroller.h"
#include "network/catserver.h"
#include "network/networkmetrics.h"
#include "controllers/statusbarcontroller.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QAction>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QDateTime>
#include <QFrame>
#include <QEvent>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QCloseEvent>
#include <QShortcut>

Q_LOGGING_CATEGORY(qk4Main, "qk4.main")

// ============== MainWindow Implementation ==============
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_radioState(new RadioState(this)) {
    setupControllers();

    // WHY: controllers referenced from setupUi() must exist at connect-time.
    // setupUi() wires BottomMenuBar signals to MenuController::toggleOverlay
    // (among others) — if m_menuController is uninitialized the connect() call
    // segfaults inside QObjectPrivate::connectImpl. Create controllers first,
    // then let setupUi() build widgets and wire them into the existing
    // controllers. VFO widgets are late-injected into PopupManager via
    // setVfos() after setupUi() creates them.
    m_menuController = new MenuController(m_connectionController, m_spectrumController, this, this);
    connect(m_menuController, &MenuController::overlayClosed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMenuActive(false);
    });
    m_popupManager =
        new PopupManager(m_radioState, m_connectionController, m_spectrumController, nullptr, nullptr, this, this);
    m_bandNavController = new BandNavigationController(m_radioState, m_connectionController, m_popupManager, this);
    connect(m_popupManager, &PopupManager::modeLabelRefreshNeeded, this, &MainWindow::updateModeLabels);
    m_macroController = new MacroController(m_connectionController, m_popupManager, this);
    connect(m_macroController, &MacroController::macroDialogRequested, this, [this]() {
        closeAllPopups();
        m_popupManager->openMacroDialog();
    });
    m_antennaCfgController = new AntennaConfigController(m_radioState, m_connectionController, this, this);
    m_textDecodeController = new TextDecodeController(m_radioState, m_connectionController, this, this);
    m_buttonRowDispatcher = new ButtonRowDispatcher(m_radioState, m_connectionController, m_popupManager,
                                                    m_antennaCfgController, m_textDecodeController, this);

    // IMPORTANT: setupUi() MUST be called BEFORE setupMenuBar()!
    // Qt 6.10.1 bug on macOS Tahoe: calling menuBar() before creating QRhiWidget
    // prevents the RHI backing store from being set up correctly, causing
    // "QRhiWidget: No QRhi" errors and blank panadapter display.
    setupUi();
    m_popupManager->setVfos(m_vfoA, m_vfoB);
    setupMenuBar();

    // ESC — halt all transmission regardless of which child widget has focus.
    // QShortcut fires at window scope; keyPressEvent only fires when the window frame itself has focus.
    // Clears both the K4 TX state (RX;) and QK4's internal PTT/audio state so the UI unlocks too.
    auto *escShortcut = new QShortcut(Qt::Key_Escape, this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (m_connectionController->isConnected())
            m_connectionController->sendCAT("RX;");
        m_audioController->setPttActive(false);
        m_bottomMenuBar->setPttActive(false);
    });

    setupNotificationWidget();
    setupConnectionWiring();

    setupRadioStateWiring();

    setupSpectrumDataRouting();

    setupHardwareController();

    m_kpa1500UiController = new KPA1500UiController(m_statusBarController, m_rightSidePanel->kpa1500Mini(), this);

    m_processingDisplayController = new ProcessingDisplayController(m_radioState, m_vfoA, m_vfoB, this);

    setupCatServer();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Deterministic audio teardown BEFORE the event loop exits. On Linux with
    // the PipeWire Qt Multimedia backend, destroying an active QAudioSink from
    // libc atexit races the PipeWire RT worker thread and segfaults in
    // pw_stream_dequeue_buffer. Stopping the sinks here — while the audio and
    // sidetone thread event loops are still servicing BlockingQueuedConnection
    // — guarantees no live QAudioSink/QAudioSource remains at process exit.
    if (m_audioController) {
        m_audioController->shutdown();
    }
    if (m_hardwareController) {
        m_hardwareController->shutdownSidetone();
    }
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() {
    // Disconnect all signals targeting this object FIRST — prevents queued signals
    // from arriving during partial destruction (e.g., a RadioState signal firing
    // after some child widgets are already destroyed but before MainWindow is).
    disconnect(this);

    // HardwareController handles KPOD, HaliKey, Keyer, Sidetone shutdown
    // (it's a child of MainWindow, so Qt deletes it automatically —
    //  its destructor shuts down threads in the correct order)

    // ConnectionController handles I/O thread shutdown in its destructor
    // (it's a child of MainWindow, so Qt deletes it automatically)
    // AudioController handles audio thread shutdown in its destructor

    // KPA1500UiController's own destructor disconnects its signals and
    // disconnects from the amplifier — Qt handles its deletion as a child.
}

// ============================================================================
// Phase 2 mechanical extractions — constructor setup helpers.
// Pure cut/paste from the constructor body; no behavior change. See
// PATTERNS.md → Controller Pattern for what will migrate in Phase 3.
// ============================================================================

void MainWindow::setupControllers() {
    // Connection controller owns TcpClient, I/O thread, and NetworkMetrics
    m_connectionController = new ConnectionController(m_radioState, this);

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    m_audioController = new AudioController(m_connectionController, m_radioState, this);

    // Spectrum controller owns panadapters, span buttons, and all spectrum wiring
    m_spectrumController = new SpectrumController(m_connectionController, m_radioState, this);

    // DX Cluster controller owns the cluster TCP client and spot cache
    m_dxClusterController = new DxClusterController(m_radioState, this);
    m_spectrumController->setDxClusterController(m_dxClusterController);
}

void MainWindow::setupNotificationWidget() {
    // Create notification popup for K4 error/status messages (ERxx:)
    m_notificationWidget = new NotificationWidget(this);
}

void MainWindow::setupConnectionWiring() {
    // ConnectionController signals
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            &MainWindow::onConnectionStateChanged);
    connect(m_connectionController, &ConnectionController::connectionError, this, &MainWindow::onConnectionError);
    connect(m_connectionController, &ConnectionController::radioReady, this, &MainWindow::onRadioReady);
    connect(m_connectionController, &ConnectionController::authFailed, this, &MainWindow::onAuthFailed);

    // Protocol CAT responses -> RadioState (via ConnectionController re-emitted signal)
    connect(m_connectionController, &ConnectionController::catResponseReceived, this, &MainWindow::onCatResponse);
}

void MainWindow::setupRadioStateWiring() {
    // RadioState signals -> UI updates (VFO A)
    connect(m_radioState, &RadioState::frequencyChanged, this, &MainWindow::onFrequencyChanged);
    connect(m_radioState, &RadioState::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) {
        onVoxChanged(false); // Refresh VOX display when mode changes (VOX is mode-specific)
    });
    // Data sub-mode changes also update mode label (AFSK, FSK, PSK, DATA)
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int) { updateModeLabels(); });
    connect(m_radioState, &RadioState::sMeterChanged, this, &MainWindow::onSMeterChanged);

    // RadioState signals -> UI updates (VFO B)
    connect(m_radioState, &RadioState::frequencyBChanged, this, &MainWindow::onFrequencyBChanged);
    connect(m_radioState, &RadioState::modeBChanged, this, &MainWindow::onModeBChanged);
    // Data sub-mode changes also update mode label (AFSK, FSK, PSK, DATA)
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int) { updateModeLabels(); });
    connect(m_radioState, &RadioState::sMeterBChanged, this, &MainWindow::onSMeterBChanged);
    // Auto-hide mini pan B when VFOs move to different bands (and SUB RX is off)
    connect(m_radioState, &RadioState::frequencyChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);
    connect(m_radioState, &RadioState::frequencyBChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);

    // RadioState signals -> Status bar updates
    connect(m_radioState, &RadioState::rfPowerChanged, this, &MainWindow::onRfPowerChanged);
    connect(m_radioState, &RadioState::supplyVoltageChanged, this, &MainWindow::onSupplyVoltageChanged);
    connect(m_radioState, &RadioState::supplyCurrentChanged, this, &MainWindow::onSupplyCurrentChanged);
    connect(m_radioState, &RadioState::swrChanged, this, &MainWindow::onSwrChanged);

    // Display FPS (synthetic menu item)
    connect(m_radioState, &RadioState::displayFpsChanged, this, &MainWindow::onDisplayFpsChanged);

    // Error/notification messages from K4 (ERxx: format) -> show notification popup
    connect(m_radioState, &RadioState::errorNotificationReceived, this, &MainWindow::onErrorNotification);

    // TX Meter data -> update power displays and VFO multifunction meters during TX
    connect(m_radioState, &RadioState::txMeterChanged, this, [this](int alc, int comp, double fwdPower, double swr) {
        m_statusBarController->setForwardPower(fwdPower);
        // Update side panel power reading
        m_sideControlPanel->setPowerReading(fwdPower);

        // Calculate PA drain current (Id) from forward power and supply voltage
        // Formula: Id = ForwardPower / (Voltage × Efficiency)
        // K4 PA efficiency is approximately 34% (measured: 80W @ 17A @ 13.8V)
        double voltage = m_radioState->supplyVoltage();
        double paCurrent = 0.0;
        if (voltage > 0 && fwdPower > 0) {
            constexpr double K4_PA_EFFICIENCY = 0.34; // Measured: 80W @ 17A @ 13.8V
            paCurrent = fwdPower / (voltage * K4_PA_EFFICIENCY);
        }

        // Update TX meters only on the active TX VFO
        // SPLIT OFF: VFO A transmits, SPLIT ON: VFO B transmits
        if (m_radioState->splitEnabled()) {
            m_vfoB->setTxMeters(alc, comp, fwdPower, swr);
            m_vfoB->setTxMeterCurrent(paCurrent);
        } else {
            m_vfoA->setTxMeters(alc, comp, fwdPower, swr);
            m_vfoA->setTxMeterCurrent(paCurrent);
        }
    });

    // TX state changes -> switch VFO meters between S-meter (RX) and Po (TX) mode
    // Also change TX indicator color to red when transmitting
    connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool transmitting) {
        // Only the active TX VFO switches to TX meter mode
        // SPLIT OFF: VFO A transmits, SPLIT ON: VFO B transmits
        // The non-TX VFO stays in S-meter mode (showing received signal)
        if (m_radioState->splitEnabled()) {
            m_vfoA->setTransmitting(false); // VFO A stays in RX mode
            m_vfoB->setTransmitting(transmitting);
        } else {
            m_vfoA->setTransmitting(transmitting);
            m_vfoB->setTransmitting(false); // VFO B stays in RX mode
        }

        // TX indicator and triangles turn red when transmitting
        QString color = transmitting ? K4Styles::Colors::TxRed : K4Styles::Colors::AccentAmber;
        m_txIndicator->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                         .arg(color)
                                         .arg(K4Styles::Dimensions::FontSizeIndicator));
        m_txTriangle->setStyleSheet(
            QString("color: %1; font-size: %2px;").arg(color).arg(K4Styles::Dimensions::FontSizeIndicator));
        m_txTriangleB->setStyleSheet(
            QString("color: %1; font-size: %2px;").arg(color).arg(K4Styles::Dimensions::FontSizeIndicator));

        // When XIT is active, show the actual TX frequency on the TX VFO display
        // No split: VFO A displays TX freq; Split: VFO B displays TX freq
        // On return to RX, restore the normal RX frequency display
        if (m_radioState->xitEnabled()) {
            if (m_radioState->splitEnabled()) {
                onFrequencyBChanged(m_radioState->vfoB());
            } else {
                onFrequencyChanged(m_radioState->vfoA());
            }
        }
    });

    // SUB indicator - green when sub RX enabled, grey when off
    // Also updates DIV indicator since DIV requires SUB to be on
    // Also dims VFO B frequency and mode labels when SUB RX is off
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            m_subLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: black;"
                                              "font-size: %2px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::StatusGreen)
                                          .arg(K4Styles::Dimensions::FontSizeNormal));
            // If DIV is also on, light up the DIV indicator (handles timing when SB3 comes after DV1)
            if (m_radioState->diversityEnabled()) {
                m_divLabel->setStyleSheet(QString("background-color: %1;"
                                                  "color: black;"
                                                  "font-size: %2px;"
                                                  "font-weight: bold;"
                                                  "border-radius: 2px;")
                                              .arg(K4Styles::Colors::StatusGreen)
                                              .arg(K4Styles::Dimensions::FontSizeNormal));
            }
            // Restore VFO B frequency and mode to normal white
            m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::TextWhite));
            m_modeBLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::TextWhite)
                                            .arg(K4Styles::Dimensions::FontSizeLarge));
        } else {
            m_subLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: %2;"
                                              "font-size: %3px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop)
                                          .arg(K4Styles::Dimensions::FontSizeNormal));
            // DIV requires SUB - turn off DIV indicator when SUB is off
            m_divLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: %2;"
                                              "font-size: %3px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop)
                                          .arg(K4Styles::Dimensions::FontSizeNormal));
            // Dim VFO B frequency and mode to indicate SUB RX is off
            m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::InactiveGray));
            m_modeBLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::InactiveGray)
                                            .arg(K4Styles::Dimensions::FontSizeLarge));

            // Auto-hide mini pan B if VFOs are on different bands (can't have mini pan B without SUB RX)
            m_spectrumController->checkAndHideMiniPanB();
        }
    });

    // DIV indicator - green only when BOTH diversity AND sub RX are enabled
    // (DIV requires SUB to be on - can't have DIV without SUB)
    connect(m_radioState, &RadioState::diversityChanged, this, [this](bool enabled) {
        // DIV only shows green if both diversity is enabled AND sub RX is enabled
        bool showActive = enabled && m_radioState->subReceiverEnabled();
        if (showActive) {
            m_divLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: black;"
                                              "font-size: %2px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::StatusGreen)
                                          .arg(K4Styles::Dimensions::FontSizeNormal));
        } else {
            m_divLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: %2;"
                                              "font-size: %3px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop)
                                          .arg(K4Styles::Dimensions::FontSizeNormal));
        }
    });

    // VFO Lock indicators - show lock arc on VFO A/B squares when locked
    connect(m_radioState, &RadioState::lockAChanged, this, [this](bool locked) { m_vfoRow->setLockA(locked); });
    connect(m_radioState, &RadioState::lockBChanged, this, [this](bool locked) { m_vfoRow->setLockB(locked); });

    // RadioState signals -> Side control panel updates (BW/SHFT/HI/LO)
    // Helper to update all 4 filter display values (called on BW or SHFT change)
    // When B SET is enabled, shows VFO B (Sub RX) filter values instead of VFO A
    auto updateFilterDisplay = [this]() {
        bool bSet = m_radioState->bSetEnabled();

        // Get bandwidth and shift from correct VFO
        int bwHz = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();
        int shiftHz = bSet ? m_radioState->shiftBHz() : m_radioState->shiftHz();

        // BW/SHFT in kHz
        m_sideControlPanel->setBandwidth(bwHz / 1000.0);
        m_sideControlPanel->setShift(shiftHz / 1000.0);

        // Calculate and set HI/LO in kHz (clamp LO to 0, then derive HI from LO + BW)
        int lowHz = qMax(0, shiftHz - (bwHz / 2));
        int highHz = lowHz + bwHz;
        m_sideControlPanel->setHighCut(highHz / 1000.0);
        m_sideControlPanel->setLowCut(lowHz / 1000.0);
    };
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftBChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::bSetChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::keyerSpeedChanged, m_sideControlPanel, &SideControlPanel::setWpm);
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) {
        m_sideControlPanel->setPitch(pitch / 1000.0); // Hz to kHz (500Hz = 0.50)
    });
    connect(m_radioState, &RadioState::rfPowerChanged, this,
            [this](double watts, bool) { m_sideControlPanel->setPower(watts); });
    connect(m_radioState, &RadioState::qskDelayChanged, this, [this](int delay) {
        m_sideControlPanel->setDelay(delay / 100.0); // 10ms units to seconds (20 -> 0.20)
    });
    connect(m_radioState, &RadioState::rfGainChanged, m_sideControlPanel, &SideControlPanel::setMainRfGain);
    connect(m_radioState, &RadioState::squelchChanged, m_sideControlPanel, &SideControlPanel::setMainSquelch);
    connect(m_radioState, &RadioState::rfGainBChanged, m_sideControlPanel, &SideControlPanel::setSubRfGain);
    connect(m_radioState, &RadioState::squelchBChanged, m_sideControlPanel, &SideControlPanel::setSubSquelch);
    connect(m_radioState, &RadioState::micGainChanged, m_sideControlPanel, &SideControlPanel::setMicGain);
    connect(m_radioState, &RadioState::compressionChanged, m_sideControlPanel, &SideControlPanel::setCompression);
    // Mode-dependent WPM/PTCH vs MIC/CMP display
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        bool isCW = (mode == RadioState::CW || mode == RadioState::CW_R);
        m_sideControlPanel->setDisplayMode(isCW);
        // Refresh values after mode switch
        if (isCW) {
            m_sideControlPanel->setWpm(m_radioState->keyerSpeed());
            m_sideControlPanel->setPitch(m_radioState->cwPitch() / 1000.0);
        } else {
            m_sideControlPanel->setMicGain(m_radioState->micGain());
            m_sideControlPanel->setCompression(m_radioState->compression());
        }
    });

    // RadioState signals -> Center section updates
    connect(m_radioState, &RadioState::splitChanged, this, &MainWindow::onSplitChanged);
    connect(m_radioState, &RadioState::antennaChanged, this, &MainWindow::onAntennaChanged);
    connect(m_radioState, &RadioState::antennaNameChanged, this, &MainWindow::onAntennaNameChanged);
    connect(m_radioState, &RadioState::voxChanged, this, &MainWindow::onVoxChanged);
    connect(m_radioState, &RadioState::qskEnabledChanged, this, &MainWindow::onQskEnabledChanged);
    connect(m_radioState, &RadioState::testModeChanged, this, &MainWindow::onTestModeChanged);
    connect(m_radioState, &RadioState::atuModeChanged, this, &MainWindow::onAtuModeChanged);
    connect(m_radioState, &RadioState::ritXitChanged, this, [this](bool ritEnabled, bool xitEnabled, int offset) {
        if (!m_radioState->bSetEnabled()) {
            // BSET off: RIT/XIT state from VFO A
            // In split mode, XIT offset lives in RO$ (VFO B register).
            // Use RO$ when XIT is active OR when XIT was active (preserved value on toggle-off)
            int displayOffset = offset;
            if (m_radioState->splitEnabled() && !ritEnabled && m_radioState->ritXitOffsetB() != 0)
                displayOffset = m_radioState->ritXitOffsetB();
            onRitXitChanged(ritEnabled, xitEnabled, displayOffset);
        } else {
            // BSET on: RIT from VFO B (RO$); XIT from RO (no split) or RO$ (split)
            int displayOffset;
            if (xitEnabled)
                displayOffset = m_radioState->splitEnabled() ? m_radioState->ritXitOffsetB() : offset;
            else
                displayOffset = m_radioState->ritXitOffsetB();
            onRitXitChanged(m_radioState->ritEnabledB(), xitEnabled, displayOffset);
        }
    });
    connect(m_radioState, &RadioState::ritXitBChanged, this, [this](bool ritEnabled, int offset) {
        if (m_radioState->bSetEnabled()) {
            // BSET on: VFO B offset changed — update display
            onRitXitChanged(ritEnabled, m_radioState->xitEnabled(), offset);
        } else if (m_radioState->splitEnabled() && m_radioState->xitEnabled()) {
            // Split + XIT: K4 routes XIT offset to RO$ (VFO B register)
            onRitXitChanged(m_radioState->ritEnabled(), true, offset);
        }
    });
    connect(m_radioState, &RadioState::messageBankChanged, this, &MainWindow::onMessageBankChanged);

    // Filter position indicators
    connect(m_radioState, &RadioState::filterPositionChanged, this,
            [this](int pos) { m_filterAWidget->setFilterPosition(pos); });
    connect(m_radioState, &RadioState::filterPositionBChanged, this,
            [this](int pos) { m_filterBWidget->setFilterPosition(pos); });

    // Filter bandwidth and shift → FilterIndicatorWidget shape
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            [this](int bw) { m_filterAWidget->setBandwidth(bw); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            [this](int bw) { m_filterBWidget->setBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_filterAWidget->setShift(shift); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_filterBWidget->setShift(shift); });
    // Mode affects filter indicator shift center calculation
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_filterAWidget->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_filterBWidget->setMode(RadioState::modeToString(mode)); });
    // DATA submode affects filter indicator shape (RTTY dual triangles)
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_filterAWidget->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_filterBWidget->setDataSubMode(subMode); });

    // RadioState signals -> Processing state updates (AGC, PRE, ATT, NB, NR)

    // RadioState signals -> MAIN RX / SUB RX popup button label updates
    // AFX button: primary = "AFX ON/OFF", alternate = mode (DELAY/PITCH/OFF)
    connect(m_radioState, &RadioState::afxModeChanged, this, [this](int mode) {
        QString primary = (mode == 0) ? "AFX OFF" : "AFX ON";
        QString alternate;
        switch (mode) {
        case 0:
            alternate = "OFF";
            break;
        case 1:
            alternate = "DELAY";
            break;
        case 2:
            alternate = "PITCH";
            break;
        }
        if (m_popupManager->mainRxPopupAnchor())
            m_popupManager->setMainRxButtonLabel(3, primary, alternate);
        if (m_popupManager->subRxPopupAnchor())
            m_popupManager->setSubRxButtonLabel(3, primary, alternate);
    });

    // AGC button: primary = speed (AGC-S/AGC-F), alternate = ON/OFF
    connect(m_radioState, &RadioState::processingChanged, this, [this]() {
        QString primary;
        QString alternate;
        switch (m_radioState->agcSpeed()) {
        case RadioState::AGC_Off:
            primary = "AGC";
            alternate = "OFF";
            break;
        case RadioState::AGC_Slow:
            primary = "AGC-S";
            alternate = "ON";
            break;
        case RadioState::AGC_Fast:
            primary = "AGC-F";
            alternate = "ON";
            break;
        }
        if (m_popupManager->mainRxPopupAnchor())
            m_popupManager->setMainRxButtonLabel(4, primary, alternate);
    });

    connect(m_radioState, &RadioState::processingChangedB, this, [this]() {
        QString primary;
        QString alternate;
        switch (m_radioState->agcSpeedB()) {
        case RadioState::AGC_Off:
            primary = "AGC";
            alternate = "OFF";
            break;
        case RadioState::AGC_Slow:
            primary = "AGC-S";
            alternate = "ON";
            break;
        case RadioState::AGC_Fast:
            primary = "AGC-F";
            alternate = "ON";
            break;
        }
        if (m_popupManager->subRxPopupAnchor())
            m_popupManager->setSubRxButtonLabel(4, primary, alternate);
    });

    // APF button: Main RX APF state -> MAIN RX popup and VFO A indicator
    connect(m_radioState, &RadioState::apfChanged, this, [this](bool enabled, int width) {
        QString alternate;
        if (!enabled) {
            alternate = "OFF";
        } else {
            static const char *bwNames[] = {"30Hz", "50Hz", "150Hz"};
            alternate = bwNames[qBound(0, width, 2)];
        }
        if (m_popupManager->mainRxPopupAnchor())
            m_popupManager->setMainRxButtonLabel(5, "APF", alternate);
        m_vfoA->setApf(enabled, width);
    });

    // APF button: Sub RX APF state -> SUB RX popup and VFO B indicator
    connect(m_radioState, &RadioState::apfBChanged, this, [this](bool enabled, int width) {
        QString alternate;
        if (!enabled) {
            alternate = "OFF";
        } else {
            static const char *bwNames[] = {"30Hz", "50Hz", "150Hz"};
            alternate = bwNames[qBound(0, width, 2)];
        }
        if (m_popupManager->subRxPopupAnchor())
            m_popupManager->setSubRxButtonLabel(5, "APF", alternate);
        m_vfoB->setApf(enabled, width);
    });

    // RadioState waterfall height -> mini-pans (panadapter wiring is in SpectrumController)
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        m_vfoA->setMiniPanWaterfallHeight(percent);
        m_vfoB->setMiniPanWaterfallHeight(percent);
    });
}

void MainWindow::setupSpectrumDataRouting() {
    // Protocol spectrum data -> SpectrumController (via ConnectionController re-emitted signals)
    connect(m_connectionController, &ConnectionController::spectrumDataReceived, m_spectrumController,
            &SpectrumController::onSpectrumData);
    connect(m_connectionController, &ConnectionController::miniSpectrumDataReceived, m_spectrumController,
            &SpectrumController::onMiniSpectrumData);
}

void MainWindow::setupHardwareController() {
    // Hardware controller owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads
    m_hardwareController = new HardwareController(m_radioState, m_connectionController, this);

    // KPOD button presses → macro execution
    connect(m_hardwareController, &HardwareController::macroRequested, m_macroController,
            &MacroController::executeMacro);

    // HaliKey footswitch PTT → TX audio + UI indicator
    connect(m_hardwareController, &HardwareController::pttRequested, this, [this](bool active) {
        if (m_connectionController->isConnected()) {
            m_audioController->setPttActive(active);
            m_bottomMenuBar->setPttActive(active);
        }
    });
}

void MainWindow::setupCatServer() {
    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    // Apps connect using their built-in K4 support - no protocol translation needed
    m_catServer = new CatServer(m_radioState, this);
    m_catServer->setTcpClient(m_connectionController->tcpClient());

    // Forward CAT commands from external apps to the real K4
    connect(m_catServer, &CatServer::catCommandReceived, this, [this](const QString &command) {
        m_connectionController->sendCAT(command);
        // Optimistically update RadioState so the panadapter passband tracks immediately,
        // without waiting for the K4's AI4 roundtrip. Spectrum packets from the K4 arrive
        // before the CAT echo, so m_centerFreq moves while m_tunedFreq is still stale —
        // the passband goes off-screen until the echo lands. Mirrors what the VFO scroll
        // wheel handler already does: sendCAT + parseCATCommand together.
        m_radioState->parseCATCommand(command);
    });

    // Surface CAT server bind failures to the user
    connect(m_catServer, &CatServer::errorOccurred, this,
            [this](const QString &error) { qWarning() << "CAT server:" << error; });

    // TX;/RX; from external apps controls audio input gate
    // Audio stream itself triggers K4 TX - timing-critical for FT8/FT4
    connect(m_catServer, &CatServer::pttRequested, this, [this](bool on) {
        m_audioController->setPttActive(on);
        m_bottomMenuBar->setPttActive(on);
    });

    // Connect to settings for CAT server enable/disable
    connect(RadioSettings::instance(), &RadioSettings::catServerEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            m_catServer->start(RadioSettings::instance()->catServerPort());
        } else {
            m_catServer->stop();
        }
    });
    connect(RadioSettings::instance(), &RadioSettings::catServerPortChanged, this, [this](quint16 port) {
        if (RadioSettings::instance()->catServerEnabled()) {
            m_catServer->stop();
            m_catServer->start(port);
        }
    });

    // Start CAT server if enabled
    if (RadioSettings::instance()->catServerEnabled()) {
        m_catServer->start(RadioSettings::instance()->catServerPort());
    }
}

void MainWindow::setupMenuBar() {
    // Standard menu bar order: File, Connect, Tools, View, Help
    // On macOS, Qt automatically creates the app menu with About/Preferences
    menuBar()->setStyleSheet(QString("QMenuBar { background-color: %1; color: %2; }"
                                     "QMenuBar::item:selected { background-color: #333; }")
                                 .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite));

    // File menu (first, per Windows convention)
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QAction *quitAction = new QAction("E&xit", this);
    quitAction->setMenuRole(QAction::QuitRole); // macOS: moves to app menu
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(quitAction);

    // Tools menu
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *optionsAction = new QAction("&Settings...", this);
    optionsAction->setMenuRole(QAction::PreferencesRole); // macOS: moves to app menu as Preferences
    connect(optionsAction, &QAction::triggered, this, [this]() {
        if (!m_optionsDialog) {
            m_optionsDialog = new OptionsDialog(m_radioState, m_audioController, m_hardwareController, m_catServer,
                                                m_kpa1500UiController->client(), m_dxClusterController, this);
        }
        m_optionsDialog->show();
        m_optionsDialog->raise();
        m_optionsDialog->activateWindow();
    });
    toolsMenu->addAction(optionsAction);

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *aboutAction = new QAction("&About QK4", this);
    aboutAction->setMenuRole(QAction::AboutRole); // macOS: moves to app menu
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QK4",
                           QString("<h2>QK4</h2>"
                                   "<p>Version %1</p>"
                                   "<p>Remote control application for Elecraft K4 radios.</p>"
                                   "<p>Copyright &copy; 2024-2025 AI5QK</p>"
                                   "<p><a href='https://github.com/mikeg-dal/QK4'>github.com/mikeg-dal/QK4</a></p>")
                               .arg(QCoreApplication::applicationVersion()));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupUi() {
    setWindowTitle("QK4");
    setMinimumSize(1340, 840);
    resize(1340, 840); // Default to minimum size on launch

    // NOTE: Do NOT set WA_NativeWindow here!
    // Qt 6.10.1 bug on macOS Tahoe: WA_NativeWindow forces native window creation
    // before QRhiWidget can configure it for MetalSurface, causing
    // "QMetalSwapChain only supports MetalSurface windows" crash.

    setStyleSheet(QString("QMainWindow { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *centralWidget = new QWidget(this);
    centralWidget->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));
    setCentralWidget(centralWidget);

    // Main vertical layout
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top status bar — owned by StatusBarController
    m_statusBarController =
        new StatusBarController(m_radioState, m_connectionController->networkMetrics(), centralWidget, this);
    mainLayout->addWidget(m_statusBarController->widget());

    // Middle section: Side Panel + Main Content (L-shaped)
    auto *middleWidget = new QWidget(centralWidget);
    auto *middleLayout = new QHBoxLayout(middleWidget);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    // Side Control Panel (left)
    m_sideControlPanel = new SideControlPanel(middleWidget);
    middleLayout->addWidget(m_sideControlPanel);

    // Main content (VFO + Spectrum)
    auto *contentWidget = new QWidget(middleWidget);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(2);

    // VFO section (A | Center | B)
    auto *vfoWidget = new QWidget(contentWidget);
    setupVfoSection(vfoWidget);
    contentLayout->addWidget(vfoWidget);

    // Spectrum/Waterfall display (owned by SpectrumController)
    m_spectrumController->setupSpectrumUI(contentWidget, m_vfoA, m_vfoB);
    contentLayout->addWidget(m_spectrumController->spectrumContainer(), 1);

    middleLayout->addWidget(contentWidget, 1);

    // Right Side Panel (mirrors left panel dimensions)
    m_rightSidePanel = new RightSidePanel(middleWidget);
    middleLayout->addWidget(m_rightSidePanel);

    mainLayout->addWidget(middleWidget, 1);

    // Feature Menu Bar (popup, positioned above bottom menu bar when shown)
    m_featureMenuController = new FeatureMenuController(m_radioState, m_connectionController, this, this);

    // Mode popup (grid of mode buttons above the bottom menu bar) — owned
    // by ModePopupController, which also handles CAT dispatch + optimistic
    // DT updates + RadioState → popup sync. See controllers/modepopupcontroller.h.
    m_modePopupController = new ModePopupController(m_radioState, m_connectionController, this, this);

    // B SET indicator visibility and side panel indicator color
    connect(m_radioState, &RadioState::bSetChanged, this, [this](bool enabled) {
        qCDebug(qk4Main) << "B SET changed:" << enabled;
        // Show/hide B SET indicator (hide SPLIT when B SET active)
        m_bSetLabel->setVisible(enabled);
        m_splitLabel->setVisible(!enabled);

        // Change side panel BW/SHFT indicator color (cyan=MainRx, green=SubRx)
        m_sideControlPanel->setActiveReceiver(enabled);

        // Switch RIT/XIT display to match active VFO
        if (enabled) {
            onRitXitChanged(m_radioState->ritEnabledB(), m_radioState->xitEnabled(), m_radioState->ritXitOffsetB());
        } else {
            onRitXitChanged(m_radioState->ritEnabled(), m_radioState->xitEnabled(), m_radioState->ritXitOffset());
        }
    });

    // Bottom Menu Bar
    m_bottomMenuBar = new BottomMenuBar(centralWidget);
    m_popupManager->setBottomMenuBar(m_bottomMenuBar);
    mainLayout->addWidget(m_bottomMenuBar);

    // Connect side control panel icon buttons
    connect(m_sideControlPanel, &SideControlPanel::connectClicked, this, &MainWindow::showRadioManager);

    // Connect volume slider to AudioController (Main RX / VFO A)
    connect(m_sideControlPanel, &SideControlPanel::volumeChanged, this, [this](int value) {
        m_audioController->setMainVolume(value / 100.0f);
        RadioSettings::instance()->setVolume(value); // Persist setting
    });

    // Connect sub volume slider to AudioController (Sub RX / VFO B)
    // In BAL mode, this slider controls L/R balance offset instead of sub volume
    connect(m_sideControlPanel, &SideControlPanel::subVolumeChanged, this, [this](int value) {
        if (m_radioState->balanceMode() == 1) {
            // BAL mode: slider controls L/R balance (0-100 maps to -50..+50)
            int offset = value - 50;
            m_audioController->setBalanceOffset(offset);
            // Send BL command to radio with current mode and new offset
            QString sign = offset >= 0 ? "+" : "-";
            QString cmd = QString("BL1%1%2;").arg(sign).arg(qAbs(offset), 2, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->setBalance(1, offset);
        } else {
            // NOR mode: slider controls sub RX volume
            m_audioController->setSubVolume(value / 100.0f);
        }
        RadioSettings::instance()->setSubVolume(value); // Persist setting
    });

    // Connect side control panel scroll signals to CAT commands
    // After sending CAT, update RadioState optimistically (radio doesn't echo these commands)
    // Group 1: WPM/PTCH (CW mode) and MIC/CMP (Voice mode)
    connect(m_sideControlPanel, &SideControlPanel::wpmChanged, this, [this](int delta) {
        int newWpm = qBound(8, m_radioState->keyerSpeed() + delta, 50);
        m_connectionController->sendCAT(QString("KS%1;").arg(newWpm, 3, 10, QChar('0')));
        m_radioState->setKeyerSpeed(newWpm);
    });
    connect(m_sideControlPanel, &SideControlPanel::pitchChanged, this, [this](int delta) {
        int currentPitch = m_radioState->cwPitch(); // In Hz
        int newPitch = qBound(300, currentPitch + (delta * 10), 990);
        m_connectionController->sendCAT(QString("CW%1;").arg(newPitch / 10, 2, 10, QChar('0')));
        m_radioState->setCwPitch(newPitch);
    });
    connect(m_sideControlPanel, &SideControlPanel::micGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->micGain() + delta, 80);
        m_connectionController->sendCAT(QString("MG%1;").arg(newGain, 3, 10, QChar('0')));
        m_radioState->setMicGain(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::compressionChanged, this, [this](int delta) {
        int newComp = qBound(0, m_radioState->compression() + delta, 30);
        m_connectionController->sendCAT(QString("CP%1;").arg(newComp, 3, 10, QChar('0')));
        m_radioState->setCompression(newComp);
    });
    // Group 1: PWR/DLY
    // PC command uses PCnnnr; format: L=QRP (0.1-10W), H=QRO (11-110W)
    // QRP (≤10W): 0.1W increments, e.g., 10.0, 9.9, 9.8, ... 0.1
    // QRO (>10W): 1W increments, e.g., 11, 12, 13, ... 110
    connect(m_sideControlPanel, &SideControlPanel::powerChanged, this, [this](int delta) {
        double currentPower = m_radioState->rfPower();
        double newPower;

        if (currentPower <= 10.0) {
            // Currently in QRP range: 0.1W increments
            newPower = currentPower + (delta * 0.1);
            if (newPower > 10.0) {
                // Transition to QRO at 11W
                newPower = 11.0;
                int powerVal = static_cast<int>(newPower);
                m_connectionController->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(0.1, newPower, 10.0);
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 9.9W = 099
                m_connectionController->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            }
        } else {
            // Currently in QRO range: 1W increments
            newPower = currentPower + delta;
            if (newPower <= 10.0) {
                // Transition to QRP at 10.0W
                newPower = 10.0;
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 10.0W = 100
                m_connectionController->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(11.0, newPower, 110.0);
                int powerVal = static_cast<int>(newPower);
                m_connectionController->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            }
        }
        m_radioState->setRfPower(newPower);
    });
    connect(m_sideControlPanel, &SideControlPanel::delayChanged, this, [this](int delta) {
        int currentDelay = m_radioState->delayForCurrentMode();
        if (currentDelay < 0)
            currentDelay = 0;                                // Handle uninitialized
        int newDelay = qBound(0, currentDelay + delta, 255); // 0-255 = 0.00 to 2.55 seconds

        // Optimistic update - update local state immediately
        m_radioState->setDelayForCurrentMode(newDelay);

        // SD command format: SDxyzzz where x=QSK flag, y=mode (C/V/D), zzz=delay in 10ms
        // Determine mode character based on current operating mode
        QChar modeChar = 'V'; // Default to Voice
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            modeChar = 'C';
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            modeChar = 'D';
        }
        // x=0 means use specified delay (not full QSK)
        m_connectionController->sendCAT(QString("SD0%1%2;").arg(modeChar).arg(newDelay, 3, 10, QChar('0')));
    });
    // Group 2: BW/HI and SHFT/LO
    // BW command uses 10Hz units (divide by 10)
    connect(m_sideControlPanel, &SideControlPanel::bandwidthChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        int currentBw = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();

        // Mode-specific BW limits (Hz)
        int bwMin = 50, bwMax = 5000;
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D
                bwMin = 150;
                bwMax = 800;
            } else if (subMode == 3) { // PSK-D
                bwMax = 200;
            }
        }

        int newBw = qBound(bwMin, currentBw + (delta * 50), bwMax);
        QString cmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(cmd).arg(newBw / 10, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setFilterBandwidthB(newBw);
        } else {
            m_radioState->setFilterBandwidth(newBw);
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::highCutChanged, this, [this](int delta) {
        // HI adjusts upper filter edge while keeping LO fixed.
        // Both BW and IS must change. Work in decahertz (dah) to avoid rounding drift.
        // Step is 2 dah (20Hz) per scroll tick — even so IS stays on-grid.
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();
        int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
        int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

        // Mode-specific BW limits (dah) and IS-locked flag
        int bwMinDah = 5, bwMaxDah = 500;
        bool isLocked = false;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
                bwMinDah = 15;
                bwMaxDah = 80;
                isLocked = true;
            } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
                bwMaxDah = 20;
                isLocked = true;
            }
        }

        // Compute displayed (clamped) HI/LO
        int loDah = qMax(0, isDah - bwDah / 2);
        int hiDah = loDah + bwDah;

        int newHiDah = hiDah + (delta * 2);
        if (newHiDah <= loDah)
            return;

        int newBwDah = qBound(bwMinDah, newHiDah - loDah, bwMaxDah);

        QString bwCmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

        if (isLocked) {
            // IS stays fixed — only BW changes
            if (bSet)
                m_radioState->setFilterBandwidthB(newBwDah * 10);
            else
                m_radioState->setFilterBandwidth(newBwDah * 10);
        } else {
            int newIsDah =
                qBound(30, (newHiDah + loDah) / 2, (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300);
            QString isPrefix = bSet ? "IS$" : "IS";
            m_connectionController->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

            if (bSet) {
                m_radioState->setFilterBandwidthB(newBwDah * 10);
                m_radioState->setIfShiftB(newIsDah);
            } else {
                m_radioState->setFilterBandwidth(newBwDah * 10);
                m_radioState->setIfShift(newIsDah);
            }
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::shiftChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();

        // IS is locked in certain modes — ignore scroll
        if (mode == RadioState::AM || mode == RadioState::FM)
            return;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2 || subMode == 3)
                return; // FSK-D, PSK-D: IS locked
        }

        int currentShift = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
        int isMax = (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300;
        int newShift = qBound(30, currentShift + delta, isMax);
        QString prefix = bSet ? "IS$" : "IS";
        m_connectionController->sendCAT(QString("%1+%2;").arg(prefix).arg(newShift, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setIfShiftB(newShift);
        } else {
            m_radioState->setIfShift(newShift);
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::lowCutChanged, this, [this](int delta) {
        // LO adjusts lower filter edge while keeping HI fixed.
        // Both BW and IS must change. Work in decahertz (dah) to avoid rounding drift.
        // Step is 2 dah (20Hz) per scroll tick — even so IS stays on-grid.
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();
        int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
        int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

        // Mode-specific BW limits (dah) and IS-locked flag
        int bwMinDah = 5, bwMaxDah = 500;
        bool isLocked = false;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
                bwMinDah = 15;
                bwMaxDah = 80;
                isLocked = true;
            } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
                bwMaxDah = 20;
                isLocked = true;
            }
        }

        // Compute displayed (clamped) HI/LO
        int loDah = qMax(0, isDah - bwDah / 2);
        int hiDah = loDah + bwDah;

        int newLoDah = loDah + (delta * 2);
        if (newLoDah >= hiDah)
            return;

        int newBwDah = qBound(bwMinDah, hiDah - newLoDah, bwMaxDah);

        QString bwCmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

        if (isLocked) {
            // IS stays fixed — only BW changes
            if (bSet)
                m_radioState->setFilterBandwidthB(newBwDah * 10);
            else
                m_radioState->setFilterBandwidth(newBwDah * 10);
        } else {
            int newIsDah =
                qBound(30, (hiDah + newLoDah) / 2, (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300);
            QString isPrefix = bSet ? "IS$" : "IS";
            m_connectionController->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

            if (bSet) {
                m_radioState->setFilterBandwidthB(newBwDah * 10);
                m_radioState->setIfShiftB(newIsDah);
            } else {
                m_radioState->setFilterBandwidth(newBwDah * 10);
                m_radioState->setIfShift(newIsDah);
            }
        }
    });
    // Group 3: M.RF/M.SQL and S.RF/S.SQL
    // RF Gain uses RG-nn; format where nn is 00-60 (representing -0 to -60 dB attenuation)
    // Scroll up = less attenuation = decrease value, scroll down = more attenuation = increase value
    connect(m_sideControlPanel, &SideControlPanel::mainRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGain() - delta, 60);
        m_connectionController->sendCAT(QString("RG-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGain(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::mainSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevel() + delta, 29);
        m_connectionController->sendCAT(QString("SQ%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevel(newSql);
    });
    connect(m_sideControlPanel, &SideControlPanel::subRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGainB() - delta, 60);
        m_connectionController->sendCAT(QString("RG$-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGainB(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::subSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevelB() + delta, 29);
        m_connectionController->sendCAT(QString("SQ$%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevelB(newSql);
    });

    // Connect TX function button signals to CAT commands
    connect(m_sideControlPanel, &SideControlPanel::tuneClicked, this,
            [this]() { m_connectionController->sendCAT("SW16;"); });
    connect(m_sideControlPanel, &SideControlPanel::tuneLpClicked, this,
            [this]() { m_connectionController->sendCAT("SW131;"); });
    connect(m_sideControlPanel, &SideControlPanel::xmitClicked, this, [this]() {
        bool goTx = !m_radioState->isTransmitting();
        m_connectionController->sendCAT(goTx ? "TX;" : "RX;");
        m_audioController->setPttActive(goTx);
        m_bottomMenuBar->setPttActive(goTx);
    });
    connect(m_sideControlPanel, &SideControlPanel::testClicked, this,
            [this]() { m_connectionController->sendCAT("SW132;"); });
    connect(m_sideControlPanel, &SideControlPanel::atuClicked, this,
            [this]() { m_connectionController->sendCAT("SW158;"); });
    connect(m_sideControlPanel, &SideControlPanel::atuTuneClicked, this,
            [this]() { m_connectionController->sendCAT("SW40;"); });
    connect(m_sideControlPanel, &SideControlPanel::voxClicked, this,
            [this]() { m_connectionController->sendCAT("SW50;"); });
    connect(m_sideControlPanel, &SideControlPanel::qskClicked, this,
            [this]() { m_connectionController->sendCAT("SW134;"); });
    connect(m_sideControlPanel, &SideControlPanel::antClicked, this,
            [this]() { m_connectionController->sendCAT("SW60;"); });
    connect(m_sideControlPanel, &SideControlPanel::rxAntClicked, this,
            [this]() { m_connectionController->sendCAT("SW70;"); });
    connect(m_sideControlPanel, &SideControlPanel::subAntClicked, this,
            [this]() { m_connectionController->sendCAT("SW157;"); });

    // Connect MON/NORM/BAL SW commands
    connect(m_sideControlPanel, &SideControlPanel::swCommandRequested, this,
            [this](const QString &cmd) { m_connectionController->sendCAT(cmd); });

    // Connect monitor level change (ML command)
    connect(m_sideControlPanel, &SideControlPanel::monLevelChangeRequested, this, [this](int mode, int level) {
        QString cmd = QString("ML%1%2;").arg(mode).arg(level, 3, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Optimistic update
        m_radioState->setMonitorLevel(mode, level);
    });

    // Update MON overlay when RadioState changes
    connect(m_radioState, &RadioState::monitorLevelChanged, m_sideControlPanel, &SideControlPanel::updateMonitorLevel);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        // Update MON overlay mode based on current operating mode
        int monMode = 2; // Default to voice
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            monMode = 0;
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            monMode = 1;
        }
        m_sideControlPanel->updateMonitorMode(monMode);
    });

    // Connect balance wheel signal (BL command)
    connect(m_sideControlPanel, &SideControlPanel::balChangeRequested, this, [this](int mode, int offset) {
        QString sign = offset >= 0 ? "+" : "-";
        QString cmd = QString("BL%1%2%3;").arg(mode).arg(sign).arg(qAbs(offset), 2, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->setBalance(mode, offset);
    });

    // Update BAL overlay and button when RadioState changes
    connect(m_radioState, &RadioState::balanceChanged, m_sideControlPanel, &SideControlPanel::updateBalance);

    // Connect right side panel button signals to CAT commands
    // Primary (left-click) signals
    connect(m_rightSidePanel, &RightSidePanel::preClicked, this,
            [this]() { m_connectionController->sendCAT("SW61;"); });
    connect(m_rightSidePanel, &RightSidePanel::nbClicked, this, [this]() { m_connectionController->sendCAT("SW32;"); });
    connect(m_rightSidePanel, &RightSidePanel::nrClicked, this, [this]() { m_connectionController->sendCAT("SW62;"); });
    connect(m_rightSidePanel, &RightSidePanel::ntchClicked, this,
            [this]() { m_connectionController->sendCAT("SW31;"); });
    connect(m_rightSidePanel, &RightSidePanel::filClicked, this,
            [this]() { m_connectionController->sendCAT("SW33;"); });
    connect(m_rightSidePanel, &RightSidePanel::abClicked, this, [this]() { m_connectionController->sendCAT("SW41;"); });
    connect(m_rightSidePanel, &RightSidePanel::revPressed, this,
            [this]() { m_connectionController->sendCAT("SW160;"); });
    connect(m_rightSidePanel, &RightSidePanel::revReleased, this,
            [this]() { m_connectionController->sendCAT("SW161;"); });
    connect(m_rightSidePanel, &RightSidePanel::atobClicked, this,
            [this]() { m_connectionController->sendCAT("SW72;"); });
    connect(m_rightSidePanel, &RightSidePanel::spotClicked, this,
            [this]() { m_connectionController->sendCAT("SW42;"); });
    connect(m_rightSidePanel, &RightSidePanel::modeClicked, this,
            [this]() { m_modePopupController->toggleForBSet(m_bottomMenuBar); });

    // Secondary (right-click) signals - these show feature menus with toggle behavior
    // If same menu is open, close it; otherwise switch to the new menu
    auto toggleFeatureMenu = [this](FeatureMenuBar::Feature feature) {
        m_featureMenuController->toggleFeature(feature, m_bottomMenuBar);
    };
    connect(m_rightSidePanel, &RightSidePanel::attnClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::Attenuator); });
    connect(m_rightSidePanel, &RightSidePanel::levelClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::NbLevel); });
    connect(m_rightSidePanel, &RightSidePanel::adjClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::NrAdjust); });
    connect(m_rightSidePanel, &RightSidePanel::manualClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::ManualNotch); });
    connect(m_rightSidePanel, &RightSidePanel::apfClicked, this, [this]() {
        // Toggle APF on/off for Main RX or Sub RX based on B SET state
        if (m_radioState->bSetEnabled()) {
            m_connectionController->sendCAT("AP$/;"); // Sub RX toggle
        } else {
            m_connectionController->sendCAT("AP/;"); // Main RX toggle
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::splitClicked, this,
            [this]() { m_connectionController->sendCAT("SW145;"); });
    connect(m_rightSidePanel, &RightSidePanel::btoaClicked, this,
            [this]() { m_connectionController->sendCAT("SW147;"); });
    connect(m_rightSidePanel, &RightSidePanel::autoClicked, this,
            [this]() { m_connectionController->sendCAT("SW146;"); });
    // altClicked (MODE/ALT right-click) - send SW148 for ALT function
    connect(m_rightSidePanel, &RightSidePanel::altClicked, this,
            [this]() { m_connectionController->sendCAT("SW148;"); });

    // PF row primary (left-click) signals
    connect(m_rightSidePanel, &RightSidePanel::bsetClicked, this,
            [this]() { m_connectionController->sendCAT("SW44;"); });
    connect(m_rightSidePanel, &RightSidePanel::clrClicked, this,
            [this]() { m_connectionController->sendCAT("SW64;"); });
    connect(m_rightSidePanel, &RightSidePanel::ritClicked, this,
            [this]() { m_connectionController->sendCAT("SW54;"); });
    connect(m_rightSidePanel, &RightSidePanel::xitClicked, this,
            [this]() { m_connectionController->sendCAT("SW74;"); });

    // PF row secondary (right-click) signals
    // PF1-PF4 execute user-configured macros (or default K4 PF functions if no macro set)
    connect(m_rightSidePanel, &RightSidePanel::pf1Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF1);
        if (!macro.command.isEmpty()) {
            m_macroController->executeMacro(MacroIds::PF1);
        } else {
            m_connectionController->sendCAT("SW153;"); // Default: K4 PF1
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf2Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF2);
        if (!macro.command.isEmpty()) {
            m_macroController->executeMacro(MacroIds::PF2);
        } else {
            m_connectionController->sendCAT("SW154;"); // Default: K4 PF2
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf3Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF3);
        if (!macro.command.isEmpty()) {
            m_macroController->executeMacro(MacroIds::PF3);
        } else {
            m_connectionController->sendCAT("SW155;"); // Default: K4 PF3
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf4Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF4);
        if (!macro.command.isEmpty()) {
            m_macroController->executeMacro(MacroIds::PF4);
        } else {
            m_connectionController->sendCAT("SW156;"); // Default: K4 PF4
        }
    });

    // Bottom row signals (SUB, DIVERSITY, RATE, LOCK)
    connect(m_rightSidePanel, &RightSidePanel::subClicked, this,
            [this]() { m_connectionController->sendCAT("SW83;"); });
    connect(m_rightSidePanel, &RightSidePanel::diversityClicked, this,
            [this]() { m_connectionController->sendCAT("SW152;"); });
    connect(m_rightSidePanel, &RightSidePanel::rateClicked, this, [this]() {
        // Cycle fine rates: 1 Hz → 10 Hz → 100 Hz → 1 Hz
        // B-SET aware: targets VFO B (VT$) when B SET is engaged
        bool bSet = m_radioState->bSetEnabled();
        int current = bSet ? m_radioState->tuningStepB() : m_radioState->tuningStep();
        int next = (current >= 0 && current < 2) ? current + 1 : 0;
        QString cmd = QString("%1%2;").arg(bSet ? "VT$" : "VT").arg(next);
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_rightSidePanel, &RightSidePanel::khzClicked, this, [this]() {
        // Set tuning step to 1 kHz (VT3)
        // B-SET aware: targets VFO B (VT$) when B SET is engaged
        bool bSet = m_radioState->bSetEnabled();
        QString cmd = bSet ? QStringLiteral("VT$3;") : QStringLiteral("VT3;");
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_rightSidePanel, &RightSidePanel::lockAClicked, this,
            [this]() { m_connectionController->sendCAT("SW63;"); }); // Toggle Lock A
    connect(m_rightSidePanel, &RightSidePanel::lockBClicked, this,
            [this]() { m_connectionController->sendCAT("SW151;"); }); // Toggle Lock B

    // Connect memory buttons (M1-M4, REC, STORE, RCL)
    // Primary actions (left click)
    connect(m_m1Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW17;"); });
    connect(m_m2Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW51;"); });
    connect(m_m3Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW18;"); });
    connect(m_m4Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW52;"); });
    connect(m_recBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW19;"); });
    connect(m_storeBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW20;"); });
    connect(m_rclBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW34;"); });

    // Install event filters for right-click (alternate actions)
    m_recBtn->installEventFilter(this);
    m_storeBtn->installEventFilter(this);
    m_rclBtn->installEventFilter(this);

    // Connect bottom menu bar signals
    connect(m_bottomMenuBar, &BottomMenuBar::menuClicked, m_menuController, &MenuController::toggleOverlay);
    connect(m_bottomMenuBar, &BottomMenuBar::fnClicked, this, &MainWindow::toggleFnPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::displayClicked, this, &MainWindow::toggleDisplayPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::bandClicked, this, &MainWindow::toggleBandPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::mainRxClicked, this, &MainWindow::toggleMainRxPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::subRxClicked, this, &MainWindow::toggleSubRxPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::txClicked, this, &MainWindow::toggleTxPopup);

    // PTT button connections
    connect(m_bottomMenuBar, &BottomMenuBar::pttPressed, this, [this]() {
        if (m_connectionController->isConnected()) {
            m_audioController->setPttActive(true);
            m_bottomMenuBar->setPttActive(true);
        }
    });
    connect(m_bottomMenuBar, &BottomMenuBar::pttReleased, this, [this]() {
        m_audioController->setPttActive(false);
        m_bottomMenuBar->setPttActive(false);
    });

    // Note: audio buffer flushing on mode/filter changes was removed — AudioEngine now runs
    // on a dedicated thread with a properly sized jitter buffer, so stale audio lag no longer
    // occurs. Flushing would cause a brief audio dropout on every mode/filter switch.
}

void MainWindow::setupVfoSection(QWidget *parent) {
    // Main vertical layout: VFO row on top, antenna row below
    auto *mainVLayout = new QVBoxLayout(parent);
    mainVLayout->setContentsMargins(4, 4, 4, 4);
    mainVLayout->setSpacing(4);

    // Top row: VFO A | Center | VFO B
    auto *vfoRowWidget = new QWidget(parent);
    auto *layout = new QHBoxLayout(vfoRowWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ===== VFO A (Left - Amber) - Using VFOWidget =====
    m_vfoA = new VFOWidget(VFOWidget::VFO_A, parent);

    // Connect VFO A click to toggle mini-pan (send CAT to enable Mini-Pan streaming)
    connect(m_vfoA, &VFOWidget::normalContentClicked, this, [this]() {
        m_vfoA->showMiniPan();
        m_radioState->setMiniPanAEnabled(true);   // Set state BEFORE sending CAT (K4 doesn't echo)
        m_connectionController->sendCAT("#MP1;"); // Enable Mini-Pan A streaming
    });
    connect(m_vfoA, &VFOWidget::miniPanClicked, this, [this]() {
        m_radioState->setMiniPanAEnabled(false);  // Set state BEFORE sending CAT
        m_connectionController->sendCAT("#MP0;"); // Disable Mini-Pan A streaming
    });

    // Connect VFO A frequency entry - send FA command then query to refresh display
    connect(m_vfoA, &VFOWidget::frequencyEntered, this, [this](const QString &freqString) {
        // FA accepts 1-11 digits: 1-2 = MHz, 3-5 = kHz, 6+ = Hz
        m_connectionController->sendCAT(QString("FA%1;FA;").arg(freqString));
    });

    // Connect VFO A wheel tuning - same pattern as panadapter wheel tuning
    connect(m_vfoA, &VFOWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        quint64 currentFreq = m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    // Set Mini-Pan A passband color to cyan (matching VFO A theme)
    QColor vfoAPassband(K4Styles::Colors::VfoACyan);
    vfoAPassband.setAlpha(64);
    m_vfoA->setMiniPanPassbandColor(vfoAPassband);

    m_vfoA->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_vfoA, 1);

    // ===== Center Section =====
    auto *centerWidget = new QWidget(parent);
    centerWidget->setFixedWidth(330);
    centerWidget->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));
    auto *centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(4, 1, 4, 4);
    centerLayout->setSpacing(3);

    // Row 1: VFO Row with absolute positioning for perfect TX centering
    // Uses VfoRowWidget to position A, TX, B, SUB/DIV independently
    m_vfoRow = new VfoRowWidget(centerWidget);
    centerLayout->addWidget(m_vfoRow);

    // Filter/RIT/XIT row — declared early so it can be added to layout here,
    // populated later after the RIT/XIT box and filter widgets are constructed
    auto *filterRitXitRow = new QHBoxLayout();
    centerLayout->addLayout(filterRitXitRow);

    // Get pointers to VfoRowWidget children for signal connections
    m_vfoASquare = m_vfoRow->vfoASquare();
    m_vfoBSquare = m_vfoRow->vfoBSquare();
    m_modeALabel = m_vfoRow->modeALabel();
    m_modeBLabel = m_vfoRow->modeBLabel();
    m_txIndicator = m_vfoRow->txIndicator();
    m_txIndicator->setCursor(Qt::PointingHandCursor);
    m_txIndicator->installEventFilter(this);
    m_txTriangle = m_vfoRow->txTriangle();
    m_txTriangleB = m_vfoRow->txTriangleB();
    m_subLabel = m_vfoRow->subLabel();
    m_divLabel = m_vfoRow->divLabel();

    // Install event filters for clickable labels
    m_vfoASquare->installEventFilter(this);
    m_vfoBSquare->installEventFilter(this);
    m_modeALabel->installEventFilter(this);
    m_modeBLabel->installEventFilter(this);

    // SPLIT, B SET, and MSG Bank labels live in VfoRowWidget (positioned under TX)
    m_splitLabel = m_vfoRow->splitLabel();
    m_bSetLabel = m_vfoRow->bSetLabel();
    m_msgBankLabel = m_vfoRow->msgBankLabel();

    // RIT/XIT Box with border - constrained size
    // Supports mouse wheel to adjust RIT/XIT offset
    m_ritXitBox = new QWidget(centerWidget);
    m_ritXitBox->setObjectName("ritXitBox");
    m_ritXitBox->setStyleSheet(QString("#ritXitBox { border: 1px solid %1; }").arg(K4Styles::Colors::InactiveGray));
    m_ritXitBox->setMaximumWidth(80);
    m_ritXitBox->setMaximumHeight(40);
    m_ritXitBox->installEventFilter(this);
    auto *ritXitLayout = new QVBoxLayout(m_ritXitBox);
    ritXitLayout->setContentsMargins(1, 2, 1, 2);
    ritXitLayout->setSpacing(1);

    auto *ritXitLabelsRow = new QHBoxLayout();
    ritXitLabelsRow->setContentsMargins(11, 0, 11, 0);
    ritXitLabelsRow->setSpacing(8);

    m_ritLabel = new QLabel("RIT", m_ritXitBox);
    m_ritLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                  .arg(K4Styles::Colors::InactiveGray)
                                  .arg(K4Styles::Dimensions::FontSizeMedium));
    m_ritLabel->setCursor(Qt::PointingHandCursor);
    m_ritLabel->installEventFilter(this);
    ritXitLabelsRow->addWidget(m_ritLabel);

    m_xitLabel = new QLabel("XIT", m_ritXitBox);
    m_xitLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                  .arg(K4Styles::Colors::InactiveGray)
                                  .arg(K4Styles::Dimensions::FontSizeMedium));
    m_xitLabel->setCursor(Qt::PointingHandCursor);
    m_xitLabel->installEventFilter(this);
    ritXitLabelsRow->addWidget(m_xitLabel);

    ritXitLabelsRow->setAlignment(Qt::AlignCenter);
    ritXitLayout->addLayout(ritXitLabelsRow);

    // Separator line between labels and value (spans full width)
    auto *ritXitSeparator = new QFrame(m_ritXitBox);
    ritXitSeparator->setFrameShape(QFrame::HLine);
    ritXitSeparator->setFrameShadow(QFrame::Plain);
    ritXitSeparator->setStyleSheet(QString("background-color: %1; border: none;").arg(K4Styles::Colors::InactiveGray));
    ritXitSeparator->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    ritXitLayout->addWidget(ritXitSeparator);

    m_ritXitValueLabel = new QLabel("+0.00", m_ritXitBox);
    m_ritXitValueLabel->setAlignment(Qt::AlignCenter);
    m_ritXitValueLabel->setStyleSheet(
        QString("color: %1; font-size: %2px; font-weight: bold; border: none; padding: 0 11px;")
            .arg(K4Styles::Colors::InactiveGray)
            .arg(K4Styles::Dimensions::FontSizePopup)); // Grey until RIT/XIT is enabled
    m_ritXitValueLabel->installEventFilter(this);
    ritXitLayout->addWidget(m_ritXitValueLabel);

    // Populate filter/RIT/XIT row (layout was declared and added to centerLayout earlier)
    filterRitXitRow->setContentsMargins(0, 0, 0, 0);
    filterRitXitRow->setSpacing(0);

    // VFO A filter indicator (cyan — matches VFO A square/slider theme)
    m_filterAWidget = new FilterIndicatorWidget(centerWidget);
    const QColor vfoACyan(K4Styles::Colors::VfoACyan);
    m_filterAWidget->setShapeColor(vfoACyan, vfoACyan);

    // VFO B filter indicator (green — matches VFO B square/slider theme)
    m_filterBWidget = new FilterIndicatorWidget(centerWidget);
    const QColor vfoBGreen(K4Styles::Colors::VfoBGreen);
    m_filterBWidget->setShapeColor(vfoBGreen, vfoBGreen);

    // Layout: [stretch] [FIL_A] [spacer] [RIT/XIT] [spacer] [FIL_B] [stretch]
    // Spacers push filters outward to align under VFO A/B squares above
    filterRitXitRow->addStretch();
    filterRitXitRow->addWidget(m_filterAWidget);
    filterRitXitRow->addSpacing(18);
    filterRitXitRow->addWidget(m_ritXitBox);
    filterRitXitRow->addSpacing(18);
    filterRitXitRow->addWidget(m_filterBWidget);
    filterRitXitRow->addStretch();

    // VOX / ATU / QSK indicator row (fixed-height container so visibility toggles don't shift layout)
    auto *indicatorContainer = new QWidget(centerWidget);
    indicatorContainer->setFixedHeight(K4Styles::Dimensions::DialogMargin);
    auto *indicatorLayout = new QHBoxLayout(indicatorContainer);
    indicatorLayout->setContentsMargins(0, 0, 0, 0);
    indicatorLayout->setSpacing(8);

    indicatorLayout->addStretch();

    // VOX indicator - orange when on, grey when off
    m_voxLabel = new QLabel("VOX", indicatorContainer);
    m_voxLabel->setAlignment(Qt::AlignCenter);
    m_voxLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_voxLabel);

    // ATU indicator (orange when AUTO, grey when off)
    m_atuLabel = new QLabel("ATU", indicatorContainer);
    m_atuLabel->setAlignment(Qt::AlignCenter);
    m_atuLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_atuLabel);

    // QSK indicator - white when on, grey when off
    m_qskLabel = new QLabel("QSK", indicatorContainer);
    m_qskLabel->setAlignment(Qt::AlignCenter);
    m_qskLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_qskLabel);

    indicatorLayout->addStretch();

    centerLayout->addWidget(indicatorContainer);

    // ===== Memory Buttons Row (M1-M4, REC, STORE, RCL) =====
    centerLayout->addStretch(); // Push buttons to vertical center

    // Helper lambda to create memory button with optional sub-label
    // Uses sidePanelButton/sidePanelButtonLight styles for consistency
    // Container: VBox with 2px spacing, button centered, sub-label below
    // Button: MemoryButtonWidth x ButtonHeightSmall (42x28)
    // Sub-label: FontSizeSmall (8px), AccentAmber color
    auto createMemoryButton = [centerWidget](const QString &label, const QString &subLabel,
                                             bool isLighter) -> QWidget * {
        auto *container = new QWidget(centerWidget);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        auto *btn = new QPushButton(label, container);
        btn->setFixedSize(K4Styles::Dimensions::MemoryButtonWidth, K4Styles::Dimensions::ButtonHeightSmall);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(isLighter ? K4Styles::sidePanelButtonLight() : K4Styles::sidePanelButton());
        layout->addWidget(btn, 0, Qt::AlignHCenter);

        // Add sub-label if provided
        if (!subLabel.isEmpty()) {
            auto *sub = new QLabel(subLabel, container);
            sub->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::AccentAmber)
                                   .arg(K4Styles::Dimensions::FontSizeSmall));
            sub->setAlignment(Qt::AlignCenter);
            layout->addWidget(sub);
        }

        return container;
    };

    // Single row: M1-M4 group, REC, STORE, RCL (all centered)
    auto *memoryRow = new QHBoxLayout();
    memoryRow->setContentsMargins(0, 0, 0, 0);
    memoryRow->setSpacing(4);

    memoryRow->addStretch();

    // M1-M4 group with MESSAGE label underneath
    auto *messageGroup = new QWidget(centerWidget);
    auto *messageGroupLayout = new QVBoxLayout(messageGroup);
    messageGroupLayout->setContentsMargins(0, 0, 0, 0);
    messageGroupLayout->setSpacing(2);

    // M1-M4 button row
    auto *m1m4Row = new QHBoxLayout();
    m1m4Row->setContentsMargins(0, 0, 0, 0);
    m1m4Row->setSpacing(4);

    // Helper to create just a button (no sub-label container)
    // Button: MemoryButtonWidth x ButtonHeightSmall (42x28), dark sidePanelButton style
    auto createSimpleButton = [centerWidget](const QString &label) -> QPushButton * {
        auto *btn = new QPushButton(label, centerWidget);
        btn->setFixedSize(K4Styles::Dimensions::MemoryButtonWidth, K4Styles::Dimensions::ButtonHeightSmall);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(K4Styles::sidePanelButton());
        return btn;
    };

    m_m1Btn = createSimpleButton("M1");
    m1m4Row->addWidget(m_m1Btn);

    m_m2Btn = createSimpleButton("M2");
    m1m4Row->addWidget(m_m2Btn);

    m_m3Btn = createSimpleButton("M3");
    m1m4Row->addWidget(m_m3Btn);

    m_m4Btn = createSimpleButton("M4");
    m1m4Row->addWidget(m_m4Btn);

    messageGroupLayout->addLayout(m1m4Row);

    // MESSAGE label with connecting lines: ——— MESSAGE ———
    auto *messageLabel = new QWidget(messageGroup);
    auto *messageLabelLayout = new QHBoxLayout(messageLabel);
    messageLabelLayout->setContentsMargins(0, 0, 0, 0);
    messageLabelLayout->setSpacing(2);

    auto *leftLine = new QFrame(messageLabel);
    leftLine->setFrameShape(QFrame::HLine);
    leftLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    leftLine->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);

    auto *msgText = new QLabel("MESSAGE", messageLabel);
    msgText->setStyleSheet(QString("color: %1; font-size: %2px;")
                               .arg(K4Styles::Colors::BorderSelected)
                               .arg(K4Styles::Dimensions::FontSizeSmall));
    msgText->setAlignment(Qt::AlignCenter);

    auto *rightLine = new QFrame(messageLabel);
    rightLine->setFrameShape(QFrame::HLine);
    rightLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    rightLine->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);

    messageLabelLayout->addWidget(leftLine, 1);
    messageLabelLayout->addWidget(msgText, 0);
    messageLabelLayout->addWidget(rightLine, 1);

    messageGroupLayout->addWidget(messageLabel);
    memoryRow->addWidget(messageGroup);

    // REC (dark grey like M keys, BANK sub-label)
    auto *recContainer = createMemoryButton("REC", "BANK", false);
    m_recBtn = recContainer->findChild<QPushButton *>();
    memoryRow->addWidget(recContainer);

    // STORE (lighter grey, AF REC sub-label)
    auto *storeContainer = createMemoryButton("STORE", "AF REC", true);
    m_storeBtn = storeContainer->findChild<QPushButton *>();
    memoryRow->addWidget(storeContainer);

    // RCL (lighter grey, AF PLAY sub-label)
    auto *rclContainer = createMemoryButton("RCL", "AF PLAY", true);
    m_rclBtn = rclContainer->findChild<QPushButton *>();
    memoryRow->addWidget(rclContainer);

    memoryRow->addStretch();
    centerLayout->addLayout(memoryRow);

    centerLayout->addStretch(); // Balance below
    layout->addWidget(centerWidget);

    // ===== VFO B (Right - Cyan) - Using VFOWidget =====
    m_vfoB = new VFOWidget(VFOWidget::VFO_B, parent);

    // Set Mini-Pan B passband color to green (matching VFO B theme)
    QColor vfoBPassband(K4Styles::Colors::VfoBGreen);
    vfoBPassband.setAlpha(64);
    m_vfoB->setMiniPanPassbandColor(vfoBPassband);

    // Connect VFO B click to toggle mini-pan (send CAT to enable Mini-Pan streaming)
    // Only allow mini pan B if SUB RX is on or VFOs are on the same band
    connect(m_vfoB, &VFOWidget::normalContentClicked, this, [this]() {
        // Block mini pan B if VFOs are on different bands and SUB RX is off
        // (K4 cannot provide separate Sub RX spectrum without SUB RX enabled)
        if (RadioUtils::getBandFromFrequency(m_radioState->vfoA()) !=
                RadioUtils::getBandFromFrequency(m_radioState->vfoB()) &&
            !m_radioState->subReceiverEnabled()) {
            qCDebug(qk4Main) << "Mini-Pan B blocked: VFOs on different bands and SUB RX is off";
            return;
        }
        m_vfoB->showMiniPan();
        m_radioState->setMiniPanBEnabled(true);    // Set state BEFORE sending CAT (K4 doesn't echo)
        m_connectionController->sendCAT("#MP$1;"); // Enable Mini-Pan B (Sub RX) streaming
    });
    connect(m_vfoB, &VFOWidget::miniPanClicked, this, [this]() {
        m_radioState->setMiniPanBEnabled(false);   // Set state BEFORE sending CAT
        m_connectionController->sendCAT("#MP$0;"); // Disable Mini-Pan B streaming
    });

    // Connect VFO B frequency entry - send FB command then query to refresh display
    connect(m_vfoB, &VFOWidget::frequencyEntered, this, [this](const QString &freqString) {
        // FB accepts 1-11 digits: 1-2 = MHz, 3-5 = kHz, 6+ = Hz
        m_connectionController->sendCAT(QString("FB%1;FB;").arg(freqString));
    });

    // Connect VFO B wheel tuning - same pattern as panadapter wheel tuning
    connect(m_vfoB, &VFOWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        quint64 currentFreq = m_radioState->vfoB();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    m_vfoB->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_vfoB, 1);

    // Add the VFO row to main layout
    mainVLayout->addWidget(vfoRowWidget);

    // NOTE: TX meters are now integrated into VFOWidgets as multifunction S/Po meters
    // (see VFOWidget::m_txMeter - displays S-meter when RX, Po when TX)

    // ===== Antenna Row (below VFO section) =====
    // Layout: [RX Ant A (left)] --- [TX Antenna (center)] --- [RX Ant B (right)]
    auto *antennaRow = new QHBoxLayout();
    antennaRow->setContentsMargins(8, 0, 8, 0);
    antennaRow->setSpacing(0);

    // RX Antenna A (Main) - white color, left-justified
    m_rxAntALabel = new QLabel("1:ANT1", parent);
    m_rxAntALabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_rxAntALabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_rxAntALabel);

    antennaRow->addStretch(1); // Push TX antenna to center

    // TX Antenna - orange color, centered
    m_txAntennaLabel = new QLabel("1:ANT1", parent);
    m_txAntennaLabel->setAlignment(Qt::AlignCenter);
    m_txAntennaLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::AccentAmber)
                                        .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_txAntennaLabel);

    antennaRow->addStretch(1); // Push RX Ant B to right

    // RX Antenna B (Sub) - white color, right-justified
    m_rxAntBLabel = new QLabel("1:ANT1", parent);
    m_rxAntBLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_rxAntBLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_rxAntBLabel);

    mainVLayout->addLayout(antennaRow);
}

QString MainWindow::formatFrequency(quint64 freq) {
    QString freqStr = QString::number(freq);
    while (freqStr.length() < 8) {
        freqStr.prepend('0');
    }

    // Insert dots: XX.XXX.XXX
    QString formatted;
    int len = freqStr.length();
    for (int i = 0; i < len; i++) {
        formatted.append(freqStr[i]);
        int posFromEnd = len - i - 1;
        if (posFromEnd > 0 && posFromEnd % 3 == 0) {
            formatted.append('.');
        }
    }

    // Remove leading zero for frequencies < 10 MHz (40m-160m)
    if (formatted.startsWith('0')) {
        formatted = formatted.mid(1);
    }
    return formatted;
}

void MainWindow::showRadioManager() {
    RadioManagerDialog dialog(this);
    connect(&dialog, &RadioManagerDialog::connectRequested, this, &MainWindow::connectToRadio);
    connect(&dialog, &RadioManagerDialog::disconnectRequested, this, [this]() {
        // TcpClient::disconnectFromHost() sends RRN; automatically
        m_connectionController->disconnectFromRadio();
    });

    // Send SL live if changed while connected (K4 does not echo SL, so update optimistically)
    connect(&dialog, &RadioManagerDialog::streamingLatencyChanged, this, [this](int tier) {
        if (m_connectionController->isConnected()) {
            m_connectionController->sendCAT(QString("SL%1;").arg(tier));
            m_radioState->parseCATCommand(QString("SL%1;").arg(tier));
        }
    });

    // Set the connected host so dialog can show "Disconnect" for active connection
    if (m_connectionController->isConnected()) {
        dialog.setConnectedHost(m_connectionController->currentRadio().host);
    }

    dialog.exec();
}

void MainWindow::connectToRadio(const RadioEntry &radio) {
    m_statusBarController->setTitle("Elecraft K4 - " + radio.name);
    m_connectionController->connectToRadio(radio);
}

void MainWindow::onConnectionStateChanged(TcpClient::ConnectionState state) {
    updateConnectionState(state);
}

void MainWindow::onConnectionError(const QString &error) {
    m_statusBarController->setConnectionStatus("Error: " + error,
                                               QString("color: %1; font-size: %2px; font-weight: bold;")
                                                   .arg(K4Styles::Colors::TxRed)
                                                   .arg(K4Styles::Dimensions::FontSizeButton));
}

void MainWindow::onRadioReady() {
    qCDebug(qk4Main) << "Successfully authenticated with K4 radio";

    // WHY we send SL here and also call parseCATCommand optimistically:
    // The K4 does NOT echo `SL` (streaming latency) responses — it silently applies the new tier
    // without ever sending back `SL<n>;`. Two consequences:
    //   (1) We must send `SL<n>;` AFTER the `RDY;` dump so our choice overrides whatever tier
    //       the radio booted with; sending before RDY would be trampled by the dump's playback.
    //   (2) Because no echo ever arrives, RadioState would never learn the new value from the
    //       normal parse path. We update it optimistically so AudioEngine frame sizing and the
    //       UI display tier match what we just requested. Tier→packet-size map verified in
    //       `memory/k4-streaming-latency.md`.
    int sl = m_connectionController->currentRadio().streamingLatency;
    m_connectionController->sendCAT(QString("SL%1;").arg(sl));
    m_radioState->parseCATCommand(QString("SL%1;").arg(sl));

    // Start audio engine via AudioController
    m_audioController->startAudio(m_sideControlPanel->volume() / 100.0f, m_sideControlPanel->subVolume() / 100.0f,
                                  RadioSettings::instance()->micGain() / 100.0f);

    // Most state is already included in the RDY; response from TcpClient.
    // Only query commands NOT included in RDY dump:
    m_connectionController->sendCAT("#DSM;");  // Display mode (LCD) - not in RDY
    m_connectionController->sendCAT("#HDSM;"); // Display mode (EXT) - not in RDY
    m_connectionController->sendCAT("#FRZ;");  // Freeze - not in RDY
    m_connectionController->sendCAT(
        "#FPS15;"); // Set display FPS to 15 on connect (12 default is too slow for large monitors)
    m_connectionController->sendCAT("#FPS;"); // Query back to confirm and update menu
    m_connectionController->sendCAT("#SCL;"); // Panadapter scale - not in RDY, needed for dB range
    // Note: ML and KP commands come in RDY; dump - no need to query

    // Sync element length with K4 server (sent in RDY dump as KZLnn)
    if (m_radioState->keyerSpeed() > 0) {
        int ditMs = 1200 / m_radioState->keyerSpeed();
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
    }

    // Create synthetic "Display FPS" menu item (will update from radio echo)
    m_menuController->addSyntheticDisplayFpsItem(15);

    // Startup macro is sent pre-RDY by TcpClient so the state dump reflects changes.

    // KPA connectivity is gated on K4 connectivity — see KPA1500UiController.
    m_kpa1500UiController->connectIfEnabled();

    // Auto-connect all DX cluster entries marked for auto-connect
    QString dxCall = RadioSettings::instance()->dxClusterCallsign();
    if (!dxCall.isEmpty()) {
        auto dxClusters = RadioSettings::instance()->dxClusters();
        for (int i = 0; i < dxClusters.size(); ++i) {
            if (dxClusters[i].autoConnect && !dxClusters[i].host.isEmpty()) {
                m_dxClusterController->connectCluster(i, dxClusters[i].host, dxClusters[i].port, dxCall);
            }
        }
    }
}

void MainWindow::onAuthFailed() {
    qCDebug(qk4Main) << "Authentication failed";
    m_statusBarController->setConnectionStatus("Auth Failed", QString("color: %1; font-size: %2px; font-weight: bold;")
                                                                  .arg(K4Styles::Colors::TxRed)
                                                                  .arg(K4Styles::Dimensions::FontSizeButton));
}

void MainWindow::onCatResponse(const QString &response) {

    // Parse CAT commands (may contain multiple commands separated by ;)
    QStringList commands = response.split(';', Qt::SkipEmptyParts);
    for (const QString &cmd : commands) {
        // PONG is handled by TcpClient for latency measurement — skip
        if (cmd.startsWith("PONG"))
            continue;

        m_radioState->parseCATCommand(cmd + ";");

        // Parse MEDF (menu definitions) from RDY response
        if (cmd.startsWith("MEDF")) {
            m_menuController->menuModel()->parseMEDF(cmd + ";");
        }
        // Route ME (menu value) commands to MenuModel for real-time updates
        else if (cmd.startsWith("ME")) {
            m_menuController->menuModel()->parseME(cmd + ";");
        }
        // Parse BN$ (Band Number) response for VFO B (Sub RX)
        else if (cmd.startsWith("BN$")) {
            // VFO B band number: BN$nn where nn is 00-10 or 16-25
            bool ok;
            int bandNum = cmd.mid(3, 2).toInt(&ok);
            if (ok)
                m_bandNavController->setCurrentBand(bandNum, true);
        }
        // Parse BN (Band Number) response for VFO A
        else if (cmd.startsWith("BN")) {
            // VFO A band number: BNnn where nn is 00-10 or 16-25
            bool ok;
            int bandNum = cmd.mid(2, 2).toInt(&ok);
            if (ok)
                m_bandNavController->setCurrentBand(bandNum, false);
        }
    }
}

void MainWindow::onFrequencyChanged(quint64 freq) {
    // When transmitting with XIT (no split): show TX frequency (dial + XIT offset)
    // When receiving with RIT: show RX frequency (dial + RIT offset)
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && !m_radioState->splitEnabled()) {
        qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabled()) {
        qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoA->setFrequency(formatFrequency(freq));
}

void MainWindow::onFrequencyBChanged(quint64 freq) {
    // When transmitting with XIT (split): show TX frequency (dial + XIT offset)
    // When receiving with RIT B: show RX frequency (dial + RIT B offset)
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && m_radioState->splitEnabled()) {
        qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabledB()) {
        qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoB->setFrequency(formatFrequency(freq));
}

void MainWindow::onModeChanged(RadioState::Mode mode) {
    // Use full mode string which includes data sub-mode (AFSK, FSK, PSK, DATA)
    // Also adds "+" suffix for USB/LSB when ESSB is enabled
    updateModeLabels();

    // Swap TX popup buttons 5/6 between CW keyer controls and SSB BW/ESSB
    if (m_popupManager->txPopupAnchor()) {
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            // CW mode: swap to paddle/iambic and keying weight buttons
            QChar iambic = m_radioState->iambicMode();
            QChar paddle = m_radioState->paddleOrientation();
            int weight = m_radioState->keyingWeight();
            if (!iambic.isNull() && !paddle.isNull()) {
                QString paddleStr = (paddle == 'R') ? "PDL REV" : "PDL NOR";
                QString iambicStr = QString("IAMB %1").arg(iambic);
                m_popupManager->setTxButtonLabel(5, paddleStr, iambicStr, true);
            } else {
                // KP state not yet received — show defaults
                m_popupManager->setTxButtonLabel(5, "PDL NOR", "IAMB A", true);
            }
            if (weight >= 90 && weight <= 125) {
                QString weightStr = QString::number(weight / 100.0, 'f', 2);
                m_popupManager->setTxButtonLabel(6, "WEIGHT", weightStr, false);
            } else {
                m_popupManager->setTxButtonLabel(6, "WEIGHT", "1.00", false);
            }
        } else {
            // Voice/data mode: restore SSB BW and ESSB buttons
            int bw = m_radioState->ssbTxBw();
            if (bw >= 24 && bw <= 45) {
                QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
                m_popupManager->setTxButtonLabel(5, "SSB BW", bwStr, false);
            } else {
                m_popupManager->setTxButtonLabel(5, "SSB BW", "2.8k", false);
            }
            m_popupManager->setTxButtonLabel(6, "ESSB", m_radioState->essbEnabled() ? "ON" : "OFF", false);
        }
    }
}

void MainWindow::onModeBChanged(RadioState::Mode mode) {
    Q_UNUSED(mode)
    // Use full mode string which includes data sub-mode (AFSK, FSK, PSK, DATA)
    updateModeLabels();
}

void MainWindow::updateModeLabels() {
    // VFO A mode label
    QString modeA = m_radioState->modeStringFull();
    RadioState::Mode mode = m_radioState->mode();
    if (m_radioState->essbEnabled() && (mode == RadioState::USB || mode == RadioState::LSB)) {
        modeA += "+";
    }
    m_modeALabel->setText(modeA);

    // VFO B mode label
    QString modeB = m_radioState->modeStringFullB();
    RadioState::Mode modeVfoB = m_radioState->modeB();
    if (m_radioState->essbEnabled() && (modeVfoB == RadioState::USB || modeVfoB == RadioState::LSB)) {
        modeB += "+";
    }
    m_modeBLabel->setText(modeB);
}

void MainWindow::onSMeterChanged(double value) {
    m_vfoA->setSMeterValue(value);
}

void MainWindow::onSMeterBChanged(double value) {
    m_vfoB->setSMeterValue(value);
}

void MainWindow::updateConnectionState(TcpClient::ConnectionState state) {
    switch (state) {
    case TcpClient::Disconnected:
        m_statusBarController->showDisconnected();
        resetUiForDisconnect();
        break;
    case TcpClient::Connecting:
    case TcpClient::Authenticating:
        m_statusBarController->showConnecting();
        break;
    case TcpClient::Connected:
        m_statusBarController->showConnected();
        break;
    }
}

// WHY: split out from updateConnectionState so the Disconnected branch is
// scannable. Each owning controller / widget absorbs its own subset of the
// reset; this helper covers only the labels MainWindow still owns directly
// (mode / antenna / split / bset / sub-div / RIT-XIT / ATU / VOX-QSK / msg).
void MainWindow::resetUiForDisconnect() {
    m_audioController->stopAudio();
    m_spectrumController->clearDisplays();
    m_vfoA->resetToDefaults();
    m_vfoB->resetToDefaults();
    m_vfoRow->setLockA(false);
    m_vfoRow->setLockB(false);
    m_vfoRow->setTestVisible(false);
    m_sideControlPanel->resetToDefaults();
    m_filterAWidget->resetToDefaults();
    m_filterBWidget->resetToDefaults();
    m_statusBarController->clearReadings();
    m_menuController->menuModel()->clear();
    m_kpa1500UiController->disconnectFromHost();
    m_radioState->reset();

    // MainWindow-owned labels that no controller has absorbed yet.
    m_modeALabel->setText("");
    m_modeBLabel->setText("");
    m_txAntennaLabel->setText("");
    m_rxAntALabel->setText("");
    m_rxAntBLabel->setText("");
    m_txTriangle->setText("◀");
    m_txTriangleB->setText("");
    m_bSetLabel->setVisible(false);
    m_ritXitValueLabel->setText("+0.00");
    m_msgBankLabel->setText("MSG: I");
    m_msgBankLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeButton));
    m_splitLabel->setText("SPLIT OFF");
    m_splitLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizeButton));

    // Disabled / inactive styling for state labels.
    const QString disabledLarge = QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::InactiveGray)
                                      .arg(K4Styles::Dimensions::FontSizeLarge);
    const QString greyedLarge = QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextGray)
                                    .arg(K4Styles::Dimensions::FontSizeLarge);
    m_modeBLabel->setStyleSheet(disabledLarge);
    m_ritLabel->setStyleSheet(disabledLarge);
    m_xitLabel->setStyleSheet(disabledLarge);
    m_atuLabel->setStyleSheet(greyedLarge);
    m_voxLabel->setStyleSheet(greyedLarge);
    m_qskLabel->setStyleSheet(greyedLarge);
    m_ritXitValueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                          .arg(K4Styles::Colors::InactiveGray)
                                          .arg(K4Styles::Dimensions::FontSizePopup));

    const QString subDivDisabled =
        QString("background-color: %1; color: %2; font-size: %3px; font-weight: bold; border-radius: 2px;")
            .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop)
            .arg(K4Styles::Dimensions::FontSizeNormal);
    m_subLabel->setStyleSheet(subDivDisabled);
    m_divLabel->setStyleSheet(subDivDisabled);

    // VFO B dim — SUB off state.
    m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::InactiveGray));
}

void MainWindow::onRfPowerChanged(double watts, bool isQrp) {
    Q_UNUSED(watts)
    // Propagate QRP mode to TX meter widgets so they use the correct scale
    m_vfoA->setTxMeterQrp(isQrp);
    m_vfoB->setTxMeterQrp(isQrp);
}

void MainWindow::onSupplyVoltageChanged(double volts) {
    m_sideControlPanel->setVoltage(volts);
}

void MainWindow::onSupplyCurrentChanged(double amps) {
    m_sideControlPanel->setCurrent(amps);
}

void MainWindow::onSwrChanged(double swr) {
    m_sideControlPanel->setSwr(swr);
}

void MainWindow::onDisplayFpsChanged(int fps) {
    // Update synthetic menu item value with whatever the radio reports
    m_menuController->setDisplayFps(fps);
}

void MainWindow::onSplitChanged(bool enabled) {
    if (enabled) {
        m_splitLabel->setText("SPLIT ON");
        m_splitLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::AccentAmber)
                                        .arg(K4Styles::Dimensions::FontSizeButton));
        // When split is on, TX goes to VFO B - clear left triangle, show right triangle
        m_txTriangle->setText("");
        m_txTriangleB->setText("▶");
    } else {
        m_splitLabel->setText("SPLIT OFF");
        m_splitLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::AccentAmber)
                                        .arg(K4Styles::Dimensions::FontSizeButton));
        // When split is off, TX stays on VFO A - show left triangle, clear right triangle
        m_txTriangle->setText("◀");
        m_txTriangleB->setText("");
    }
    // Split changes which VFO transmits — update TX markers
    m_spectrumController->updateTxMarkers();
}

void MainWindow::onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub) {
    // Format Main RX antenna display based on AR command value
    // K4 AR command values (from official K4 protocol documentation):
    // 0 = Disconnected (all RX RF sources disconnected)
    // 1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
    // 2 = RX USES TX ANT (follows TX antenna selection) - show resolved value
    // 3 = INT. XVTR IN (internal transverter)
    // 4 = RX ANT IN1 (receive antenna jack)
    // 5 = ATU RX ANT1 (TX antenna 1 via ATU)
    // 6 = ATU RX ANT2 (TX antenna 2 via ATU)
    // 7 = ATU RX ANT3 (TX antenna 3 via ATU)
    auto formatMainRxAntenna = [this, txAnt](int arValue) -> QString {
        switch (arValue) {
        case 0: // Disconnected
            return "OFF";
        case 1: // EXT. XVTR IN / RX ANT IN2
            return QString("RX2:%1").arg(m_radioState->antennaName(5));
        case 2: // RX USES TX ANT - show resolved value like K4 front panel
            return QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt));
        case 3: // INT. XVTR IN
            return "INT XVTR";
        case 4: // RX ANT IN1
            return QString("RX1:%1").arg(m_radioState->antennaName(4));
        case 5: // ATU RX ANT1
            return QString("1:%1").arg(m_radioState->antennaName(1));
        case 6: // ATU RX ANT2
            return QString("2:%1").arg(m_radioState->antennaName(2));
        case 7: // ATU RX ANT3
            return QString("3:%1").arg(m_radioState->antennaName(3));
        default:
            return QString("AR%1").arg(arValue);
        }
    };

    // Format Sub RX antenna display based on AR$ command value
    // K4 AR$ command values (from official K4 protocol documentation):
    // 0 = Disconnected (all RX RF sources disconnected)
    // 1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
    // 2 = RX USES TX ANT (follows TX antenna selection) - show resolved value
    // 3 = INT. XVTR IN (internal transverter)
    // 4 = RX ANT IN1 (receive antenna jack)
    // 5 = ATU RX ANT1 (TX antenna 1 via ATU)
    // 6 = ATU RX ANT2 (TX antenna 2 via ATU)
    // 7 = ATU RX ANT3 (TX antenna 3 via ATU)
    auto formatSubRxAntenna = [this, txAnt](int arValue) -> QString {
        switch (arValue) {
        case 0: // Disconnected
            return "OFF";
        case 1: // EXT. XVTR IN / RX ANT IN2
            return QString("RX2:%1").arg(m_radioState->antennaName(5));
        case 2: // RX USES TX ANT - show resolved value like K4 front panel
            return QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt));
        case 3: // INT. XVTR IN
            return "INT XVTR";
        case 4: // RX ANT IN1
            return QString("RX1:%1").arg(m_radioState->antennaName(4));
        case 5: // ATU RX ANT1
            return QString("1:%1").arg(m_radioState->antennaName(1));
        case 6: // ATU RX ANT2
            return QString("2:%1").arg(m_radioState->antennaName(2));
        case 7: // ATU RX ANT3
            return QString("3:%1").arg(m_radioState->antennaName(3));
        default:
            return QString("AR$%1").arg(arValue);
        }
    };

    // TX antenna (AN command) - always 1-3, format as "N:name"
    m_txAntennaLabel->setText(QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt)));

    // RX antennas - Main (AR) and Sub (AR$) have different value mappings
    m_rxAntALabel->setText(formatMainRxAntenna(rxAntMain));
    m_rxAntBLabel->setText(formatSubRxAntenna(rxAntSub));
}

void MainWindow::onAntennaNameChanged(int index, const QString &name) {
    Q_UNUSED(index)
    Q_UNUSED(name)
    // Refresh antenna displays when a name changes. Popup-name updates are
    // handled internally by AntennaConfigController via its own observer.
    onAntennaChanged(m_radioState->txAntenna(), m_radioState->rxAntennaMain(), m_radioState->rxAntennaSub());
}

void MainWindow::onVoxChanged(bool enabled) {
    Q_UNUSED(enabled)
    // Use mode-specific VOX state (CW modes use VXC, Voice modes use VXV, Data modes use VXD)
    bool voxOn = m_radioState->voxForCurrentMode();
    if (voxOn) {
        m_voxLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    } else {
        m_voxLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::TextGray)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    }
}

void MainWindow::onQskEnabledChanged(bool enabled) {
    // QSK indicator: white when enabled, grey when disabled
    if (enabled) {
        m_qskLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::TextWhite)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    } else {
        m_qskLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::TextGray)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    }
}

void MainWindow::onTestModeChanged(bool enabled) {
    // TEST indicator: visible in red when test mode is on
    m_vfoRow->setTestVisible(enabled);
}

void MainWindow::onAtuModeChanged(int mode) {
    // ATU indicator: orange when AUTO mode (2), grey otherwise
    if (mode == 2) {
        m_atuLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    } else {
        m_atuLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                      .arg(K4Styles::Colors::TextGray)
                                      .arg(K4Styles::Dimensions::FontSizeLarge));
    }
}

void MainWindow::onRitXitChanged(bool ritEnabled, bool xitEnabled, int offset) {
    // Update RIT label
    if (ritEnabled) {
        m_ritLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; border: none;")
                                      .arg(K4Styles::Colors::TextWhite)
                                      .arg(K4Styles::Dimensions::FontSizeMedium));
    } else {
        m_ritLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                      .arg(K4Styles::Colors::InactiveGray)
                                      .arg(K4Styles::Dimensions::FontSizeMedium));
    }

    // Update XIT label
    if (xitEnabled) {
        m_xitLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; border: none;")
                                      .arg(K4Styles::Colors::TextWhite)
                                      .arg(K4Styles::Dimensions::FontSizeMedium));
    } else {
        m_xitLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                      .arg(K4Styles::Colors::InactiveGray)
                                      .arg(K4Styles::Dimensions::FontSizeMedium));
    }

    // Update offset value (in kHz)
    // Value is white if RIT or XIT is on, grey if both are off
    double offsetKHz = offset / 1000.0;
    QString sign = (offset >= 0) ? "+" : "";
    m_ritXitValueLabel->setText(QString("%1%2").arg(sign).arg(offsetKHz, 0, 'f', 2));

    QString valueColor = (ritEnabled || xitEnabled) ? K4Styles::Colors::TextWhite : K4Styles::Colors::InactiveGray;
    m_ritXitValueLabel->setStyleSheet(
        QString("color: %1; font-size: %2px; font-weight: bold; border: none; padding: 0 11px;")
            .arg(valueColor)
            .arg(K4Styles::Dimensions::FontSizePopup));

    // Refresh frequency displays and panadapter passband — RIT offset affects receive frequency
    onFrequencyChanged(m_radioState->vfoA());
    onFrequencyBChanged(m_radioState->vfoB());

    // Update panadapter passband positions (tuned frequency includes RIT offset when active)
    // When BSET is on, panadapter A shows VFO B's passband position (matching the UI switch)
    m_spectrumController->updatePanadapterPassbands();

    // Update TX marker — shows where we'll transmit when RIT/XIT splits TX from RX
    m_spectrumController->updateTxMarkers();
}

void MainWindow::onMessageBankChanged(int bank) {
    if (bank == 1) {
        m_msgBankLabel->setText("MSG: I");
    } else {
        m_msgBankLabel->setText("MSG: II");
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // Clicks on VFO-A square / mode label → mode popup targeted at VFO A.
    if ((watched == m_vfoASquare || watched == m_modeALabel) && event->type() == QEvent::MouseButtonPress) {
        m_modePopupController->toggleForVfoA(m_bottomMenuBar);
        return true;
    }

    // Clicks on VFO-B square / mode label → mode popup targeted at VFO B.
    if ((watched == m_vfoBSquare || watched == m_modeBLabel) && event->type() == QEvent::MouseButtonPress) {
        m_modePopupController->toggleForVfoB(m_bottomMenuBar);
        return true;
    }

    // Panadapter resize events handled by SpectrumController's eventFilter

    // Handle right-click on memory buttons (alternate actions)
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            if (watched == m_recBtn) {
                m_connectionController->sendCAT("SW137;"); // BANK
                return true;
            } else if (watched == m_storeBtn) {
                m_connectionController->sendCAT("SW138;"); // AF REC
                return true;
            } else if (watched == m_rclBtn) {
                m_connectionController->sendCAT("SW139;"); // AF PLAY
                return true;
            }
        }
    }

    // RIT label click - toggle RIT on/off (SW54 routes correctly when BSET targets VFO B)
    if (watched == m_ritLabel && event->type() == QEvent::MouseButtonPress) {
        bool bSet = m_radioState->bSetEnabled();
        m_connectionController->sendCAT(bSet ? "SW54;" : "RT/;");
        // K4 doesn't echo RT$/RO$ for SW54 — query VFO B RIT state
        if (bSet) {
            m_connectionController->sendCAT("RT$;");
            m_connectionController->sendCAT("RO$;");
        }
        return true;
    }

    // XIT label click - toggle XIT on/off
    if (watched == m_xitLabel && event->type() == QEvent::MouseButtonPress) {
        m_connectionController->sendCAT("XT/;");
        return true;
    }

    // TX indicator click - toggle split on/off
    if (watched == m_txIndicator && event->type() == QEvent::MouseButtonPress) {
        m_connectionController->sendCAT("SW145;");
        return true;
    }

    // Mouse wheel on RIT/XIT box (or its child widgets) - adjust offset using RU/RD commands
    // K4 routes RU;/RD; based on active mode: RIT → RO (VFO A), XIT → RO$ (VFO B)
    // BSET + RIT: use RU$/RD$ to force VFO B's RIT offset
    if (event->type() == QEvent::Wheel &&
        (watched == m_ritXitBox || watched == m_ritLabel || watched == m_xitLabel || watched == m_ritXitValueLabel)) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        int steps = m_ritWheelAccumulator.accumulate(wheelEvent);
        if (steps != 0) {
            bool bSet = m_radioState->bSetEnabled();
            bool adjustB = bSet && !m_radioState->xitEnabled();
            QString upCmd = adjustB ? "RU$;" : "RU;";
            QString downCmd = adjustB ? "RD$;" : "RD;";
            for (int i = 0; i < qAbs(steps); ++i)
                m_connectionController->sendCAT(steps > 0 ? upCmd : downCmd);
        }
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    closeAllPopups();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Handle F1-F12 for keyboard macros
    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12) {
        int fKeyNum = event->key() - Qt::Key_F1 + 1; // 1-12
        QString functionId = QString("Keyboard-F%1").arg(fKeyNum);
        m_macroController->executeMacro(functionId);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// WHY: toggle handlers close only popups NOT owned by PopupManager before
// delegating to PopupManager::toggleX. PopupManager::toggleX captures the
// target popup's visibility up-front, then calls its own closeOwnedPopups —
// so if closeAllPopups ran first, toggleX would see the popup as already
// hidden and always re-open it (i.e., the toggle would never close).
void MainWindow::closeNonPopupManagerPopups() {
    if (m_menuController && m_menuController->isOverlayVisible()) {
        m_menuController->closeOverlay();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMenuActive(false);
    }
    m_antennaCfgController->closeAll();
    if (m_modePopupController)
        m_modePopupController->close();
}

void MainWindow::closeAllPopups() {
    closeNonPopupManagerPopups();
    m_popupManager->closeOwnedPopups();
}

void MainWindow::toggleDisplayPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleDisplay();
}

void MainWindow::toggleBandPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleBand();
}

void MainWindow::toggleFnPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleFn();
}

void MainWindow::toggleMainRxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleMainRx();
}

void MainWindow::toggleSubRxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleSubRx();
}

void MainWindow::toggleTxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleTx();
}

// ============== K4 Error/Notification Slots ==============

void MainWindow::onErrorNotification(int errorCode, const QString &message) {
    Q_UNUSED(errorCode)
    // Show the notification message in a centered popup
    // The message contains the text after "ERxx:" (e.g., "KPA1500 Status: operate.")
    if (m_notificationWidget) {
        m_notificationWidget->showMessage(message, 2000);
    }
}
