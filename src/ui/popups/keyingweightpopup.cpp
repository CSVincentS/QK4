#include "ui/popups/keyingweightpopup.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
const int MinWeight = 90;
const int MaxWeight = 125;
const int WeightStep = 5;
const int TitleWidth = 150;
const int ValueWidth = 80;
} // namespace

KeyingWeightPopupWidget::KeyingWeightPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void KeyingWeightPopupWidget::setupUi() {
    auto *layout = new QHBoxLayout(this);
    // Original tight margins (sm + 8 vertical, sm + 12 horizontal) — base's
    // contentMargins() returns sm + 12 all around which is 4px taller than
    // this popup's original layout.
    int sm = K4Styles::Dimensions::ShadowMargin;
    layout->setContentsMargins(sm + ContentMargin, sm + 8, sm + ContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("KEYING WEIGHT", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_valueLabel = new QPushButton("1.00", this);
    m_valueLabel->setFixedSize(ValueWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setFocusPolicy(Qt::NoFocus);
    m_valueLabel->setStyleSheet(QString("QPushButton {"
                                        "  color: %1;"
                                        "  font-size: %2px;"
                                        "  font-weight: 600;"
                                        "  background: transparent;"
                                        "  border: %3px solid transparent;"
                                        "  border-radius: %4px;"
                                        "}")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::PopupValueSize)
                                    .arg(K4Styles::Dimensions::BorderWidth)
                                    .arg(K4Styles::Dimensions::BorderRadius));

    m_decrementBtn = new QPushButton("-", this);
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_closeBtn = new QPushButton("↩", this); // U+21A9 leftwards arrow with hook
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(-WeightStep); });
    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(WeightStep); });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);

    updateValueDisplay();
}

QSize KeyingWeightPopupWidget::contentSize() const {
    // Inner content (excludes the base's shadow margins, which it adds back).
    // Original outer widget was 480x92 (with shadow); inner is 440x52.
    const int contentWidth = ContentMargin + TitleWidth + 6 + ValueWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             6 + K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             ContentMargin;
    return QSize(contentWidth, ContentHeight);
}

void KeyingWeightPopupWidget::updateValueDisplay() {
    double ratio = m_weight / 100.0;
    m_valueLabel->setText(QString::number(ratio, 'f', 2));
}

void KeyingWeightPopupWidget::adjustValue(int delta) {
    int newWeight = qBound(MinWeight, m_weight + delta, MaxWeight);
    if (newWeight != m_weight) {
        m_weight = newWeight;
        updateValueDisplay();
        emit weightChanged(newWeight);
    }
}

void KeyingWeightPopupWidget::setWeight(int weight) {
    m_weight = qBound(MinWeight, weight, MaxWeight);
    updateValueDisplay();
}

void KeyingWeightPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps * WeightStep);
    event->accept();
}

void KeyingWeightPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    // Override the base's solid PopupBackground with this popup's gradient +
    // border + delimiter-line treatment to preserve the original look.
    QLinearGradient grad = K4Styles::buttonGradient(contentRect.top(), contentRect.bottom());
    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, K4Styles::Dimensions::BorderRadiusLarge,
                            K4Styles::Dimensions::BorderRadiusLarge);

    // Vertical delimiter lines: one after the title, one after the increment button.
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    int lineTop = contentRect.top() + 7;
    int lineBottom = contentRect.bottom() - 7;

    auto drawDelimiter = [&](QWidget *widget) {
        if (widget && widget->isVisible()) {
            int x = widget->geometry().right() + 3;
            painter.drawLine(x, lineTop, x, lineBottom);
        }
    };

    drawDelimiter(m_titleLabel);
    drawDelimiter(m_incrementBtn);
}
