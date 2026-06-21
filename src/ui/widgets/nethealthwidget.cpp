#include "ui/widgets/nethealthwidget.h"
#include "ui/styling/k4constants.h"
#include "ui/widgets/nethealthpopup.h"
#include <QEvent>
#include <QPainter>

NetHealthWidget::NetHealthWidget(NetworkMetrics *metrics, QWidget *parent) : QWidget(parent), m_metrics(metrics) {
    setFixedSize(K4Styles::Dimensions::SmallIconSize, 16);
    setCursor(Qt::PointingHandCursor); // hint: click to open the metrics popup
    connect(m_metrics, &NetworkMetrics::healthTierChanged, this, &NetHealthWidget::onHealthTierChanged);
}

NetHealthWidget::~NetHealthWidget() {
    hideMetricsPopup();
}

void NetHealthWidget::onHealthTierChanged(NetworkMetrics::HealthTier tier) {
    m_tier = tier;
    // NetworkMetrics only emits Green/Yellow/Orange when connected; Red on disconnect
    // But Red can also mean connected-but-degraded, so track explicitly
    m_connected = (tier != NetworkMetrics::Red) || (m_metrics->rttCurrent() >= 0);
    update();
}

void NetHealthWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 4 ascending bars: bar heights as fraction of widget height
    static constexpr int BarCount = 4;
    static constexpr double barHeights[BarCount] = {0.25, 0.50, 0.75, 1.0};

    // How many bars are "lit" by tier
    int litBars = 0;
    QColor litColor;
    if (m_connected) {
        switch (m_tier) {
        case NetworkMetrics::Green:
            litBars = 4;
            litColor = QColor(K4Styles::Colors::StatusGreen);
            break;
        case NetworkMetrics::Yellow:
            litBars = 3;
            litColor = QColor(K4Styles::Colors::MeterYellow);
            break;
        case NetworkMetrics::Orange:
            litBars = 2;
            litColor = QColor(K4Styles::Colors::MeterOrange);
            break;
        case NetworkMetrics::Red:
        default:
            litBars = 1;
            litColor = QColor(K4Styles::Colors::TxRed);
            break;
        }
    }

    QColor dimColor(K4Styles::Colors::InactiveGray);

    int barWidth = 3;
    int gap = 2;
    int totalWidth = BarCount * barWidth + (BarCount - 1) * gap;
    int xOffset = (width() - totalWidth) / 2;

    for (int i = 0; i < BarCount; i++) {
        int barH = static_cast<int>(height() * barHeights[i]);
        int x = xOffset + i * (barWidth + gap);
        int y = height() - barH;

        p.setPen(Qt::NoPen);
        p.setBrush(i < litBars ? litColor : dimColor);
        p.drawRect(x, y, barWidth, barH);
    }
}

void NetHealthWidget::mousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    // Click-toggle: click the LED to open/close the live metrics popup (predictable, and avoids
    // the hover/leave-tracking that orphaned the popup across fullscreen transitions).
    toggleMetricsPopup();
}

void NetHealthWidget::toggleMetricsPopup() {
    if (m_popup)
        hideMetricsPopup();
    else
        showMetricsPopup();
}

void NetHealthWidget::showMetricsPopup() {
    if (m_popup)
        return;

    // Parent to this widget so the popup can never outlive it; clicking the popup dismisses it.
    m_popup = new NetHealthPopup(m_metrics, this);
    connect(m_popup, &NetHealthPopup::dismissed, this, &NetHealthWidget::hideMetricsPopup);
    m_popup->updatePosition(this); // reposition every open so it tracks the LED's current location
    m_popup->show();
    // Dismiss the popup if the app window moves/resizes/changes state while it's open.
    if (window())
        window()->installEventFilter(this);
}

void NetHealthWidget::hideMetricsPopup() {
    if (m_popup) {
        if (window())
            window()->removeEventFilter(this);
        m_popup->hide();
        m_popup->deleteLater();
        m_popup = nullptr;
    }
}

bool NetHealthWidget::eventFilter(QObject *watched, QEvent *event) {
    if (m_popup && watched == window()) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowStateChange: // maximize / minimize / restore / fullscreen
            hideMetricsPopup();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
