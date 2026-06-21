#ifndef NETHEALTHPOPUP_H
#define NETHEALTHPOPUP_H

#include <QWidget>
#include <deque>

#include "network/networkmetrics.h"

class QTimer;
class QMouseEvent;

/**
 * @brief Frameless hover popup that plots the last 10 seconds of network metrics as three
 *        stacked, independently auto-scaled sparklines (RTT / JIT / BUF) on a rounded,
 *        drop-shadowed dark card. Repaints on its own timer while visible so the traces
 *        scroll live; reads NetworkMetrics history getters on the main thread (no locking).
 *
 * Positioning is owned here (updatePosition): anchored above its trigger widget and clamped to
 * all four screen edges so it never clips, including the fullscreen bottom-edge case.
 */
class NetHealthPopup : public QWidget {
    Q_OBJECT
public:
    explicit NetHealthPopup(NetworkMetrics *metrics, QWidget *parent = nullptr);

    // Place the card above `anchor` (flipping below / clamping to the visible screen as needed).
    void updatePosition(QWidget *anchor);

signals:
    // Emitted when the user clicks the popup to dismiss it; the owner tears it down.
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void drawSparklineRow(QPainter &painter, const QRect &rowRect, const QString &label, const QString &valueText,
                          const char *seriesColor, int decimals, const QString &unit,
                          const std::deque<NetworkMetrics::TimedSample> &history, bool connected);

    NetworkMetrics *m_metrics;
    QTimer *m_refreshTimer;
};

#endif // NETHEALTHPOPUP_H
