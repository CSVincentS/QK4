#include "statusbarcontroller.h"

#include "controllers/connectioncontroller.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "ui/popups/confirmpopup.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/icontextlabel.h"
#include "ui/widgets/nethealthwidget.h"
#include "ui/widgets/powerstatusbutton.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace {
// 1 Hz clock tick. Sub-second drift on the status-bar clock is imperceptible;
// saves 50× timer overhead vs 50 ms updates.
constexpr int kClockUpdateIntervalMs = 1000;

// Generic "safe-for-ham-radio" PA/LPA thresholds — we don't have the K4's
// published spec for these stages, so these are conservative working bounds:
//   <  kWarnTempC  → amber (normal operating)
//   >= kWarnTempC  → orange (warming, watch it)
//   >= kCritTempC  → red (back off TX)
constexpr int kWarnTempC = 60;
constexpr int kCritTempC = 75;

QColor temperatureColor(int celsius) {
    if (celsius >= kCritTempC)
        return QColor(K4Styles::Colors::TxRed);
    if (celsius >= kWarnTempC)
        return QColor(K4Styles::Colors::MeterOrange);
    return QColor(K4Styles::Colors::AccentAmber);
}
} // namespace

StatusBarController::StatusBarController(RadioState *radioState, ConnectionController *connectionController,
                                         NetworkMetrics *networkMetrics, QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connectionController(connectionController),
      m_container(new QWidget(parentWidget)), m_clockTimer(new QTimer(this)) {
    m_container->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_container->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DarkBackground));

    auto *layout = new QHBoxLayout(m_container);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(20);

    // Elecraft K4 title
    m_titleLabel = new QLabel("Elecraft K4", m_container);
    m_titleLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: %2px;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(m_titleLabel);

    // Date/Time
    m_dateTimeLabel = new QLabel("--/-- --:--:-- Z", m_container);
    m_dateTimeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_dateTimeLabel);

    layout->addStretch();

    // Lower-PA heatsink temperature (SIRF LT, °C).
    m_lpaTempField = new IconTextLabel(m_container);
    m_lpaTempField->setLabel("LPA");
    m_lpaTempField->setUnit("°C");
    layout->addWidget(m_lpaTempField);

    // PA heatsink temperature (SIRF PT, °C).
    m_paTempField = new IconTextLabel(m_container);
    m_paTempField->setLabel("PA");
    m_paTempField->setUnit("°C");
    layout->addWidget(m_paTempField);

    // Voltage slot (was a flat label — now styled via IconTextLabel to match
    // the new bar language; icon arrives in a follow-up).
    m_voltageField = new IconTextLabel(m_container);
    m_voltageField->setUnit("V");
    layout->addWidget(m_voltageField);

    layout->addStretch();

    // KPA1500 status (to left of K4 status)
    m_kpa1500StatusLabel = new QLabel("", m_container);
    m_kpa1500StatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                            .arg(K4Styles::Colors::InactiveGray)
                                            .arg(K4Styles::Dimensions::FontSizeButton));
    m_kpa1500StatusLabel->hide(); // Hidden when not enabled
    layout->addWidget(m_kpa1500StatusLabel);

    // K4 connection status
    m_connectionStatusLabel = new QLabel("K4", m_container);
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::InactiveGray)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_connectionStatusLabel);

    // Network health signal bars
    m_netHealthWidget = new NetHealthWidget(networkMetrics, m_container);
    layout->addWidget(m_netHealthWidget);

    // Remote power button — rightmost item.
    m_powerButton = new PowerStatusButton(m_container);
    layout->addWidget(m_powerButton);
    connect(m_powerButton, &QToolButton::clicked, this, &StatusBarController::onPowerButtonClicked);

    // Voltage updates → value field.
    connect(m_radioState, &RadioState::supplyVoltageChanged, this,
            [this](double volts) { m_voltageField->setValue(QString("%1").arg(volts, 0, 'f', 1)); });

    // PA / LPA temperatures from SIRF (PT / LT fields).
    connect(m_radioState, &RadioState::paTemperatureChanged, this, [this](int c) {
        m_paTempField->setValueColor(temperatureColor(c));
        m_paTempField->setValue(QString::number(c));
    });
    connect(m_radioState, &RadioState::lpaTemperatureChanged, this, [this](int c) {
        m_lpaTempField->setValueColor(temperatureColor(c));
        m_lpaTempField->setValue(QString::number(c));
    });

    // PS0/PS1 → power button state.
    connect(m_radioState, &RadioState::powerStateChanged, this, [this](bool on) {
        m_powerButton->setState(on ? PowerStatusButton::State::On : PowerStatusButton::State::Off);
    });

    // On disconnect, blank metric fields and force the power button to "Off"
    // (it will go back to Unknown → On as soon as the next connect's PS query
    // response lands).
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    m_lpaTempField->clear();
                    m_paTempField->clear();
                    m_voltageField->clear();
                    m_powerButton->setState(PowerStatusButton::State::Off);
                } else if (state == TcpClient::Connecting || state == TcpClient::Authenticating) {
                    m_powerButton->setState(PowerStatusButton::State::Unknown);
                }
            });

    // Clock tick.
    connect(m_clockTimer, &QTimer::timeout, this, &StatusBarController::updateDateTime);
    m_clockTimer->start(kClockUpdateIntervalMs);
    updateDateTime();
}

StatusBarController::~StatusBarController() {
    // Architecture Rule 11 — disconnect first to prevent queued signals from
    // arriving during partial destruction.
    disconnect(this);
}

QWidget *StatusBarController::widget() const {
    return m_container;
}

void StatusBarController::updateDateTime() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_dateTimeLabel->setText(now.toString("M-dd / HH:mm:ss") + " Z");
}

void StatusBarController::setTitle(const QString &text) {
    m_titleLabel->setText(text);
}

void StatusBarController::showDisconnected() {
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::InactiveGray)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
    m_titleLabel->setText("Elecraft K4");
}

void StatusBarController::showConnecting() {
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::AccentAmber)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::showConnected() {
    m_connectionStatusLabel->setText("K4");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::StatusGreen)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::showError(const QString &errorMessage) {
    m_connectionStatusLabel->setText("Error: " + errorMessage);
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::TxRed)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::showAuthFailed() {
    m_connectionStatusLabel->setText("Auth Failed");
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                               .arg(K4Styles::Colors::TxRed)
                                               .arg(K4Styles::Dimensions::FontSizeButton));
}

void StatusBarController::clearReadings() {
    m_lpaTempField->clear();
    m_paTempField->clear();
    m_voltageField->clear();
}

void StatusBarController::setKpa1500Visible(bool visible) {
    m_kpa1500StatusLabel->setVisible(visible);
}

void StatusBarController::setKpa1500Status(const QString &text, const QString &styleSheet) {
    m_kpa1500StatusLabel->setText(text);
    if (!styleSheet.isEmpty())
        m_kpa1500StatusLabel->setStyleSheet(styleSheet);
}

void StatusBarController::onPowerButtonClicked() {
    if (m_confirmPopup && m_confirmPopup->isVisibleOrJustHidden())
        return;

    if (!m_confirmPopup) {
        m_confirmPopup = new ConfirmPopup(
            QStringLiteral("Power off K4?"),
            QStringLiteral("This will power off the remote K4. You will not be able to power it back on from QK4. "
                           "Continue?"),
            QStringLiteral("Power Off"), QStringLiteral("Cancel"), m_container);
        connect(m_confirmPopup, &ConfirmPopup::confirmed, this, [this]() {
            if (m_connectionController)
                m_connectionController->sendCAT(QStringLiteral("PS0;"));
            // No auto-disconnect — K4 closes the socket on its end, and the
            // existing disconnected flow takes over.
        });
    }

    m_confirmPopup->showAboveWidget(m_powerButton);
}
