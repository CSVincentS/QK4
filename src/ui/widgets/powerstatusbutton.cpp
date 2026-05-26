#include "ui/widgets/powerstatusbutton.h"

#include "ui/styling/k4constants.h"

#include <QPainter>
#include <QPixmap>

namespace {
constexpr int kIconSize = 22;
constexpr int kButtonSize = 28;

// Build a simple power-glyph pixmap at runtime. Replace by loading the SVG
// asset once the user supplies one (see plan: assets pending).
QPixmap makePowerIcon(const QColor &color) {
    QPixmap pm(kIconSize, kIconSize);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    // Open circle (lower 3/4) — universal power-symbol arc.
    const QRectF arcRect(2.5, 2.5, kIconSize - 5, kIconSize - 5);
    p.drawArc(arcRect, 110 * 16, 320 * 16);

    // Vertical stroke through the top gap.
    const qreal cx = kIconSize / 2.0;
    p.drawLine(QPointF(cx, 2), QPointF(cx, kIconSize / 2.0 + 1));

    return pm;
}
} // namespace

PowerStatusButton::PowerStatusButton(QWidget *parent) : QToolButton(parent) {
    setFixedSize(kButtonSize, kButtonSize);
    setIconSize(QSize(kIconSize, kIconSize));
    setAutoRaise(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("QToolButton { background: transparent; border: none; }");
    applyAppearance();
}

void PowerStatusButton::setState(State state) {
    if (state == m_state)
        return;
    m_state = state;
    applyAppearance();
}

void PowerStatusButton::applyAppearance() {
    QColor color;
    QString tip;
    bool clickable = false;
    switch (m_state) {
    case State::On:
        color = QColor(K4Styles::Colors::StatusGreen);
        tip = QStringLiteral("K4 powered on — click to power off remote K4");
        clickable = true;
        break;
    case State::Off:
        color = QColor(K4Styles::Colors::TxRed);
        tip = QStringLiteral("K4 not connected");
        break;
    case State::Unknown:
    default:
        color = QColor(K4Styles::Colors::InactiveGray);
        tip = QStringLiteral("K4 power state unknown");
        break;
    }
    setIcon(QIcon(makePowerIcon(color)));
    setToolTip(tip);
    setEnabled(clickable);
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}
