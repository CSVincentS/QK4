#include "statusbarcontroller.h"

#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/nethealthwidget.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace {
// 1 Hz clock tick. Sub-second drift on the status-bar clock is imperceptible;
// saves 50× timer overhead vs 50 ms updates.
constexpr int kClockUpdateIntervalMs = 1000;
} // namespace

StatusBarController::StatusBarController(RadioState *radioState, NetworkMetrics *networkMetrics, QWidget *parentWidget,
                                         QObject *parent)
    : QObject(parent), m_radioState(radioState), m_container(new QWidget(parentWidget)),
      m_clockTimer(new QTimer(this)) {
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

    // Power (TX meter-driven — see setForwardPower())
    m_powerLabel = new QLabel("--- W", m_container);
    m_powerLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_powerLabel);

    // SWR
    m_swrLabel = new QLabel("-.-:1", m_container);
    m_swrLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_swrLabel);

    // Voltage
    m_voltageLabel = new QLabel("--.- V", m_container);
    m_voltageLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_voltageLabel);

    // Current
    m_currentLabel = new QLabel("-.- A", m_container);
    m_currentLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                      .arg(K4Styles::Colors::AccentAmber)
                                      .arg(K4Styles::Dimensions::FontSizeButton));
    layout->addWidget(m_currentLabel);

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

    // Observe RadioState for supply voltage/current and SWR. These used to
    // live in MainWindow::onSupplyVoltageChanged / onSupplyCurrentChanged /
    // onSwrChanged. MainWindow keeps those slots only for the side-panel
    // companion updates; label updates happen here directly.
    connect(m_radioState, &RadioState::supplyVoltageChanged, this,
            [this](double volts) { m_voltageLabel->setText(QString("%1 V").arg(volts, 0, 'f', 1)); });
    connect(m_radioState, &RadioState::supplyCurrentChanged, this,
            [this](double amps) { m_currentLabel->setText(QString("%1 A").arg(amps, 0, 'f', 1)); });
    connect(m_radioState, &RadioState::swrChanged, this,
            [this](double swr) { m_swrLabel->setText(QString("%1:1").arg(swr, 0, 'f', 1)); });

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

void StatusBarController::setConnectionStatus(const QString &text, const QString &styleSheet) {
    m_connectionStatusLabel->setText(text);
    if (!styleSheet.isEmpty())
        m_connectionStatusLabel->setStyleSheet(styleSheet);
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

void StatusBarController::setForwardPower(double watts) {
    QString powerStr;
    if (watts < 10.0) {
        powerStr = QString("%1 W").arg(watts, 0, 'f', 1);
    } else {
        powerStr = QString("%1 W").arg(static_cast<int>(watts));
    }
    m_powerLabel->setText(powerStr);
}

void StatusBarController::clearReadings() {
    m_powerLabel->setText("--- W");
    m_swrLabel->setText("-.-:1");
    m_voltageLabel->setText("--.- V");
    m_currentLabel->setText("-.- A");
}

void StatusBarController::setKpa1500Visible(bool visible) {
    m_kpa1500StatusLabel->setVisible(visible);
}

void StatusBarController::setKpa1500Status(const QString &text, const QString &styleSheet) {
    m_kpa1500StatusLabel->setText(text);
    if (!styleSheet.isEmpty())
        m_kpa1500StatusLabel->setStyleSheet(styleSheet);
}
