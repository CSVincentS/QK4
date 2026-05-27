#include "ui/styling/k4glyphs.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

namespace K4Glyphs {

Glyph power(int sizePx) {
    return [sizePx](const QColor &color) {
        QPixmap pm(sizePx, sizePx);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, 2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        // Open arc (lower 3/4) — universal power-symbol arc.
        const QRectF arcRect(2.5, 2.5, sizePx - 5, sizePx - 5);
        p.drawArc(arcRect, 110 * 16, 320 * 16);

        // Vertical stroke through the top gap.
        const qreal cx = sizePx / 2.0;
        p.drawLine(QPointF(cx, 2), QPointF(cx, sizePx / 2.0 + 1));

        return pm;
    };
}

Glyph thermometer(int sizePx) {
    return [sizePx](const QColor &color) {
        QPixmap pm(sizePx, sizePx);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        // Geometry: vertical stem centered horizontally, filled bulb at the bottom.
        const qreal cx = sizePx / 2.0;
        const qreal bulbRadius = sizePx * 0.22;
        const qreal stemTop = sizePx * 0.12;
        const qreal stemWidth = sizePx * 0.18;
        const qreal bulbCenterY = sizePx - bulbRadius - 1.0;

        // Bulb (filled circle).
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(cx, bulbCenterY), bulbRadius, bulbRadius);

        // Stem (rounded rect from top down into the bulb).
        const QRectF stemRect(cx - stemWidth / 2.0, stemTop, stemWidth, bulbCenterY - stemTop + 1.0);
        p.drawRoundedRect(stemRect, stemWidth / 2.0, stemWidth / 2.0);

        // Two short tick marks on the right side (purely decorative — reads as
        // a thermometer at icon size; remove if visual noise).
        QPen tickPen(color, 1.0);
        tickPen.setCapStyle(Qt::RoundCap);
        p.setPen(tickPen);
        const qreal tickX = cx + stemWidth / 2.0 + 1.5;
        const qreal tickLen = sizePx * 0.12;
        p.drawLine(QPointF(tickX, sizePx * 0.32), QPointF(tickX + tickLen, sizePx * 0.32));
        p.drawLine(QPointF(tickX, sizePx * 0.52), QPointF(tickX + tickLen, sizePx * 0.52));

        return pm;
    };
}

Glyph lightning(int sizePx) {
    return [sizePx](const QColor &color) {
        QPixmap pm(sizePx, sizePx);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        // Stylized lightning bolt — six-point polygon traced from upper-left to
        // lower-right with two notches that give the classic Z shape.
        const qreal s = sizePx;
        QPainterPath bolt;
        bolt.moveTo(s * 0.55, s * 0.05);
        bolt.lineTo(s * 0.15, s * 0.55);
        bolt.lineTo(s * 0.42, s * 0.55);
        bolt.lineTo(s * 0.30, s * 0.95);
        bolt.lineTo(s * 0.85, s * 0.40);
        bolt.lineTo(s * 0.55, s * 0.40);
        bolt.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(bolt);

        return pm;
    };
}

} // namespace K4Glyphs
