#ifndef NETHEALTHWIDGET_H
#define NETHEALTHWIDGET_H

#include <QWidget>
#include "network/networkmetrics.h"

/**
 * @brief Small LED in the status bar that reflects NetworkMetrics::HealthTier (green/yellow/red)
 *        based on buffer fill and jitter. Hover pops a detail panel with live metrics.
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
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void showMetricsPopup();
    void hideMetricsPopup();

    NetworkMetrics *m_metrics;
    NetworkMetrics::HealthTier m_tier = NetworkMetrics::Red;
    bool m_connected = false;
    QWidget *m_popup = nullptr;
};

#endif // NETHEALTHWIDGET_H
