#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QMenuBar>
#include <QMenu>
#include <QTimer>
#include <QThread>
#include "controllers/connectioncontroller.h"
#include "settings/radiosettings.h"
#include "models/radiostate.h"
#include "ui/vfowidget.h"
#include "ui/wheelaccumulator.h"

class AudioController;
class SpectrumController;
class StatusBarController;
class SideControlPanel;
class RightSidePanel;
class BottomMenuBar;
class MenuController;
class PopupManager;
class AntennaConfigController;
class TextDecodeController;
class FilterIndicatorWidget;
class FeatureMenuController;
class ModePopupController;
class HardwareController;
class DxClusterController;
class KPA1500UiController;
class CatServer;
class OptionsDialog;
class NotificationWidget;
class VfoRowWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnectionStateChanged(TcpClient::ConnectionState state);
    void onConnectionError(const QString &error);
    void onRadioReady();
    void onAuthFailed();
    void onCatResponse(const QString &response);
    void onFrequencyChanged(quint64 freq);
    void onFrequencyBChanged(quint64 freq);
    void onModeChanged(RadioState::Mode mode);
    void onModeBChanged(RadioState::Mode mode);
    void onSMeterChanged(double value);
    void onSMeterBChanged(double value);
    void onRfPowerChanged(double watts, bool isQrp);
    void onSupplyVoltageChanged(double volts);
    void onSupplyCurrentChanged(double amps);
    void onSwrChanged(double swr);
    void onSplitChanged(bool enabled);
    void onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub);
    void onAntennaNameChanged(int index, const QString &name);
    void onVoxChanged(bool enabled);
    void onQskEnabledChanged(bool enabled);
    void onTestModeChanged(bool enabled);
    void onAtuModeChanged(int mode);
    void onRitXitChanged(bool ritEnabled, bool xitEnabled, int offset);
    void onMessageBankChanged(int bank);
    void onProcessingChanged();
    void onProcessingChangedB();
    void showRadioManager();
    void connectToRadio(const RadioEntry &radio);
    void onBandSelected(const QString &bandName);
    void updateBandSelection(int bandNum);
    void updateBandSelectionB(int bandNum);
    void toggleDisplayPopup();
    void toggleBandPopup();
    void toggleFnPopup();
    void toggleMainRxPopup();
    void toggleSubRxPopup();
    void toggleTxPopup();
    void closeAllPopups();

    // Error/notification from K4 (ERxx: messages)
    void onErrorNotification(int errorCode, const QString &message);

    // Display FPS (synthetic menu item)
    void onDisplayFpsChanged(int fps);

    // Fn popup / macro slots
    void onFnFunctionTriggered(const QString &functionId);
    void executeMacro(const QString &functionId);
    void openMacroDialog();

    // MAIN RX / SUB RX popup slots
    void onMainRxButtonClicked(int index);
    void onMainRxButtonRightClicked(int index);
    void onSubRxButtonClicked(int index);
    void onSubRxButtonRightClicked(int index);
    void onTxButtonClicked(int index);
    void onTxButtonRightClicked(int index);

private:
    void setupMenuBar();
    void setupUi();
    void setupVfoSection(QWidget *parent);

    // Phase 2 mechanical extractions — see PATTERNS.md / CONVENTIONS.md Rule 12.
    // These move inline constructor code into named helpers for readability;
    // no behavior change. Phase 3 lifts some of these seams into dedicated
    // controller classes under src/controllers/.
    void setupControllers();
    void setupNotificationWidget();
    void setupConnectionWiring();
    void setupRadioStateWiring();
    void setupSpectrumDataRouting();
    void setupHardwareController();
    void setupCatServer();

    void updateConnectionState(TcpClient::ConnectionState state);
    QString formatFrequency(quint64 freq);
    void updateModeLabels();

    // Closes menu overlay, antenna-config popups, and mode popup — i.e.,
    // every popup NOT owned by PopupManager. Used by the per-popup toggle
    // handlers so PopupManager::toggleX can still see its target popup's
    // current visibility state (toggle-close must work).
    void closeNonPopupManagerPopups();

    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    AudioController *m_audioController;

    // Spectrum controller owns panadapters, span buttons, VFO indicators, and spectrum wiring
    SpectrumController *m_spectrumController;

    // Top status bar — owned by StatusBarController (see src/controllers/).
    StatusBarController *m_statusBarController;

    // VFO widgets (modular, reusable components). Each owns its own multifunction S/Po/ALC/COMP/
    // SWR/Id meter (VFOWidget::m_txMeter) so there is no standalone TX meter member here.
    VFOWidget *m_vfoA;
    VFOWidget *m_vfoB;

    // Mode labels (in center section, not in VFOWidget)
    QLabel *m_modeALabel;
    QLabel *m_modeBLabel;

    // RX Antenna labels (in antenna row below VFOs)
    QLabel *m_rxAntALabel;
    QLabel *m_rxAntBLabel;

    // Center section - first row with absolute positioning
    VfoRowWidget *m_vfoRow;

    // Center section labels (pointers to VfoRowWidget children)
    QWidget *m_vfoASquare; // VfoSquareWidget - used for event filter
    QLabel *m_txTriangle;  // Left triangle (pointing at A) - shown when split OFF
    QLabel *m_txTriangleB; // Right triangle (pointing at B) - shown when split ON
    QLabel *m_txIndicator;
    QWidget *m_vfoBSquare; // VfoSquareWidget - used for event filter
    QLabel *m_splitLabel;
    QLabel *m_bSetLabel;
    QLabel *m_subLabel; // SUB indicator (green when sub RX enabled)
    QLabel *m_divLabel; // DIV indicator (green when diversity enabled)
    QLabel *m_msgBankLabel;
    QWidget *m_ritXitBox;
    QLabel *m_ritLabel;
    QLabel *m_xitLabel;
    QLabel *m_ritXitValueLabel;
    QLabel *m_atuLabel;
    FilterIndicatorWidget *m_filterAWidget; // VFO A filter indicator
    FilterIndicatorWidget *m_filterBWidget; // VFO B filter indicator

    // Memory buttons (M1-M4, REC, STORE, RCL)
    QPushButton *m_m1Btn;
    QPushButton *m_m2Btn;
    QPushButton *m_m3Btn;
    QPushButton *m_m4Btn;
    QPushButton *m_recBtn;
    QPushButton *m_storeBtn;
    QPushButton *m_rclBtn;
    QLabel *m_voxLabel;
    QLabel *m_qskLabel;
    QLabel *m_txAntennaLabel;

    // Control panels (L-shaped layout)
    SideControlPanel *m_sideControlPanel;
    RightSidePanel *m_rightSidePanel;
    BottomMenuBar *m_bottomMenuBar;

    // Menu system — owned by MenuController (src/controllers/).
    MenuController *m_menuController;
    PopupManager *m_popupManager;
    TextDecodeController *m_textDecodeController;
    AntennaConfigController *m_antennaCfgController;
    FeatureMenuController *m_featureMenuController;
    ModePopupController *m_modePopupController;

    int m_currentBandNum = -1;  // Current band number for VFO A (BN command)
    int m_currentBandNumB = -1; // Current band number for VFO B (BN$ command)

    // Hardware controller (owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads)
    HardwareController *m_hardwareController;

    // KPA1500 amplifier UI controller (owns the KPA1500Client)
    KPA1500UiController *m_kpa1500UiController;

    // DX Cluster controller
    DxClusterController *m_dxClusterController;

    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    CatServer *m_catServer;

    // Persistent Options dialog (lazy-created on first open)
    OptionsDialog *m_optionsDialog = nullptr;

    // Notification popup for K4 error/status messages (ERxx:)
    NotificationWidget *m_notificationWidget;

    WheelAccumulator m_ritWheelAccumulator;
};

#endif // MAINWINDOW_H
