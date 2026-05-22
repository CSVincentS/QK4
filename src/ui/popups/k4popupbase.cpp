#include "ui/popups/k4popupbase.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace {
// Grace period after a hide during which isVisibleOrJustHidden() stays true,
// so the click that dismissed the popup via click-away is not re-read as a
// request to reopen it. Long enough to span one button press→release.
const int ReopenGuardMs = 250;
} // namespace

K4PopupBase::K4PopupBase(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
}

QMargins K4PopupBase::contentMargins() const {
    int sm = K4Styles::Dimensions::ShadowMargin;
    int cm = K4Styles::Dimensions::PopupContentMargin;

    return QMargins(sm + cm, // left
                    sm + cm, // top
                    sm + cm, // right
                    sm + cm  // bottom
    );
}

void K4PopupBase::initPopup() {
    QSize cs = contentSize();
    int totalWidth = cs.width() + 2 * K4Styles::Dimensions::ShadowMargin;
    int totalHeight = cs.height() + 2 * K4Styles::Dimensions::ShadowMargin;
    setFixedSize(totalWidth, totalHeight);
}

QRect K4PopupBase::contentRect() const {
    QSize cs = contentSize();
    return QRect(K4Styles::Dimensions::ShadowMargin, K4Styles::Dimensions::ShadowMargin, cs.width(), cs.height());
}

void K4PopupBase::showAboveButton(QWidget *triggerButton) {
    showAboveWidget(triggerButton);
}

void K4PopupBase::showAboveWidget(QWidget *referenceWidget) {
    if (!referenceWidget)
        return;

    // Ensure our geometry is set before positioning
    adjustSize();

    // Get the reference widget's parent (typically button bar) for centering
    QWidget *parentBar = referenceWidget->parentWidget();
    if (!parentBar)
        parentBar = referenceWidget;

    // Get global positions
    QPoint barGlobal = parentBar->mapToGlobal(QPoint(0, 0));
    QPoint refGlobal = referenceWidget->mapToGlobal(QPoint(0, 0));

    int barCenterX = barGlobal.x() + parentBar->width() / 2;

    // Content dimensions (use calculated size, not widget height() which may not be realized)
    QSize cs = contentSize();
    int sm = K4Styles::Dimensions::ShadowMargin;
    int totalHeight = cs.height() + 2 * sm;

    // Center content area above the parent bar (account for shadow margin offset)
    int popupX = barCenterX - cs.width() / 2 - sm;

    // Position popup so its bottom edge (including shadow margin) is at the top of the reference widget
    int popupY = refGlobal.y() - totalHeight;

    // Ensure popup stays on screen
    QRect screenGeom = referenceWidget->screen()->availableGeometry();

    // Check left edge
    if (popupX < screenGeom.left() - K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.left() - K4Styles::Dimensions::ShadowMargin;
    }
    // Check right edge
    else if (popupX + width() > screenGeom.right() + K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.right() + K4Styles::Dimensions::ShadowMargin - width();
    }

    // Check top edge (if popup would go off top of screen)
    if (popupY < screenGeom.top() - K4Styles::Dimensions::ShadowMargin) {
        popupY = screenGeom.top() - K4Styles::Dimensions::ShadowMargin;
    }

    // Position the popup - move after show to override Qt's popup positioning
    move(popupX, popupY);
    show();
    move(popupX, popupY); // Move again after show in case Qt repositioned it
    raise();
    setFocus();
    // Watch application-wide mouse presses so a click outside the popup
    // dismisses it (click-away). Removed again in hideEvent().
    qApp->installEventFilter(this);
}

void K4PopupBase::hidePopup() {
    hide();
    // closed() signal is emitted by hideEvent()
}

bool K4PopupBase::isVisibleOrJustHidden() const {
    return isVisible() || (m_hiddenTimer.isValid() && m_hiddenTimer.elapsed() < ReopenGuardMs);
}

void K4PopupBase::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    qApp->removeEventFilter(this);
    m_hiddenTimer.restart();
    emit closed();
}

bool K4PopupBase::eventFilter(QObject *watched, QEvent *event) {
    // Click-away dismissal: a mouse press anywhere outside this popup's
    // window closes it. The press is not consumed — it still reaches its
    // target, so clicking another control works in the same gesture. The
    // toggle handlers' isVisibleOrJustHidden() guard absorbs the case where
    // that target is the popup's own trigger button.
    //
    // WHY an app-wide filter, not WindowDeactivate: these popups are Qt::Tool
    // windows parented to the main window, so Qt keeps them "active" whenever
    // the main window is active — WindowDeactivate never fires for an in-app
    // click. A position test against a global mouse press is the reliable way.
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        if (!frameGeometry().contains(globalPos))
            hidePopup();
    }
    return QWidget::eventFilter(watched, event);
}

void K4PopupBase::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void K4PopupBase::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get content rect for drawing
    QRect cr = contentRect();

    // Draw drop shadow
    K4Styles::drawDropShadow(painter, cr, K4Styles::Dimensions::BorderRadiusLarge);

    // Main popup background
    painter.setBrush(QColor(K4Styles::Colors::PopupBackground));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(cr, K4Styles::Dimensions::BorderRadiusLarge, K4Styles::Dimensions::BorderRadiusLarge);

    // Allow subclass to draw additional content
    paintContent(painter, cr);
}

void K4PopupBase::paintContent(QPainter &painter, const QRect &contentRect) {
    // Default implementation does nothing
    // Subclasses can override for custom painting
    Q_UNUSED(painter)
    Q_UNUSED(contentRect)
}
