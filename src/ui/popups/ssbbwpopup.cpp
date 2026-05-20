#include "ui/popups/ssbbwpopup.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
const int TitleWidth = 180; // "SSB TX BANDWIDTH" or "ESSB TX BANDWIDTH"
const int ValueWidth = 80;
const int SsbMinBw = 24;
const int SsbMaxBw = 28;
const int EssbMinBw = 30;
const int EssbMaxBw = 45;
} // namespace

SsbBwPopupWidget::SsbBwPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void SsbBwPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + ContentMargin, sm + 8, sm + ContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("SSB TX BANDWIDTH", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_valueLabel = new QPushButton("3.0 kHz", this);
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

    m_closeBtn = new QPushButton("↩", this); // U+21A9
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(-1); });
    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(1); });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);

    updateTitle();
    updateValueDisplay();
}

QSize SsbBwPopupWidget::contentSize() const {
    const int contentWidth = ContentMargin + TitleWidth + 6 + ValueWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             6 + K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             ContentMargin;
    return QSize(contentWidth, ContentHeight);
}

void SsbBwPopupWidget::setEssbEnabled(bool enabled) {
    if (enabled != m_essbEnabled) {
        m_essbEnabled = enabled;
        updateTitle();
    }
}

void SsbBwPopupWidget::updateTitle() {
    m_titleLabel->setText(m_essbEnabled ? "ESSB TX BANDWIDTH" : "SSB TX BANDWIDTH");
}

void SsbBwPopupWidget::updateValueDisplay() {
    double kHz = m_bandwidth / 10.0;
    m_valueLabel->setText(QString("%1 kHz").arg(kHz, 0, 'f', 1));
}

void SsbBwPopupWidget::adjustValue(int delta) {
    int minBw = m_essbEnabled ? EssbMinBw : SsbMinBw;
    int maxBw = m_essbEnabled ? EssbMaxBw : SsbMaxBw;
    int newBw = qBound(minBw, m_bandwidth + delta, maxBw);
    if (newBw != m_bandwidth) {
        m_bandwidth = newBw;
        updateValueDisplay();
        emit bandwidthChanged(newBw);
    }
}

void SsbBwPopupWidget::setBandwidth(int bw) {
    int minBw = m_essbEnabled ? EssbMinBw : SsbMinBw;
    int maxBw = m_essbEnabled ? EssbMaxBw : SsbMaxBw;
    m_bandwidth = qBound(minBw, bw, maxBw);
    updateValueDisplay();
}

void SsbBwPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps);
    event->accept();
}

void SsbBwPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    QLinearGradient grad = K4Styles::buttonGradient(contentRect.top(), contentRect.bottom());
    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, K4Styles::Dimensions::BorderRadiusLarge,
                            K4Styles::Dimensions::BorderRadiusLarge);

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
