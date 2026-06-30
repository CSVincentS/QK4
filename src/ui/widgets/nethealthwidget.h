#ifndef NETHEALTHWIDGET_H
#define NETHEALTHWIDGET_H

#include <QWidget>
#include "network/networkmetrics.h"

class NetHealthPopup;
class QMouseEvent;

/**
 * @brief Small LED in the status bar that reflects NetworkMetrics::HealthTier (green/yellow/red)
 *        based on buffer fill and jitter. Hover pops a live sparkline detail panel.
 */
class NetHealthWidget : public QWidget {
    Q_OBJECT
public:
    explicit NetHealthWidget(NetworkMetrics *metrics, QWidget *parent = nullptr);
    ~NetHealthWidget() override;

public slots:
    void onHealthTierChanged(NetworkMetrics::HealthTier tier);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    // Watches the top-level window while the popup is open: any move/resize/state change dismisses
    // the popup so it can't stay locked to a stale position (matches the app's other popups).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleMetricsPopup();
    void showMetricsPopup();
    void hideMetricsPopup();

    NetworkMetrics *m_metrics;
    NetworkMetrics::HealthTier m_tier = NetworkMetrics::Red;
    bool m_connected = false;
    NetHealthPopup *m_popup = nullptr;
};

#endif // NETHEALTHWIDGET_H
