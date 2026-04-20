#ifndef STATUSBARCONTROLLER_H
#define STATUSBARCONTROLLER_H

#include <QObject>

class RadioState;
class NetworkMetrics;
class QWidget;
class QLabel;
class QTimer;
class NetHealthWidget;

// Owns the top status bar: title, date/time clock, power/SWR/voltage/current
// readings, KPA1500 status, K4 connection status, and NetHealthWidget.
//
// Observes RadioState directly for voltage/current/SWR updates so MainWindow
// no longer needs onSupplyVoltageChanged / onSupplyCurrentChanged / onSwrChanged
// slots for the purpose of updating status-bar labels. MainWindow keeps those
// slots only to update the side panel readouts (will collapse further once
// SideControlPanel absorbs them in Phase 4).
//
// Forward power (the power label) is TX-meter-driven, not RFPower-driven, so
// the controller exposes setForwardPower() for MainWindow's txMeterChanged
// handler to call.
//
// See PATTERNS.md → Controller Pattern.
class StatusBarController : public QObject {
    Q_OBJECT

public:
    explicit StatusBarController(RadioState *radioState, NetworkMetrics *networkMetrics, QWidget *parentWidget,
                                 QObject *parent = nullptr);
    ~StatusBarController() override;

    // Container widget — MainWindow adds this into its top layout.
    QWidget *widget() const;

    // Task-level API. Caller passes the styleSheet string because the
    // color/bold combinations for connection status are numerous and live
    // more naturally in MainWindow's state-transition logic (updateConnectionState).
    void setTitle(const QString &text);
    void setConnectionStatus(const QString &text, const QString &styleSheet);
    void setForwardPower(double watts);
    void clearReadings();
    void setKpa1500Visible(bool visible);
    void setKpa1500Status(const QString &text, const QString &styleSheet);

private:
    RadioState *m_radioState; // injected, not owned
    QWidget *m_container;     // owned via Qt parent (parentWidget)
    QLabel *m_titleLabel;
    QLabel *m_dateTimeLabel;
    QLabel *m_powerLabel;
    QLabel *m_swrLabel;
    QLabel *m_voltageLabel;
    QLabel *m_currentLabel;
    QLabel *m_connectionStatusLabel;
    QLabel *m_kpa1500StatusLabel;
    NetHealthWidget *m_netHealthWidget;
    QTimer *m_clockTimer;

    void updateDateTime();
};

#endif // STATUSBARCONTROLLER_H
