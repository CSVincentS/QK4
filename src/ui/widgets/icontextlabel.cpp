#include "ui/widgets/icontextlabel.h"

#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QLabel>

namespace {
constexpr int kIconSize = K4Styles::Dimensions::SmallIconSize;
constexpr const char *kEmptyPlaceholder = "--";
} // namespace

IconTextLabel::IconTextLabel(QWidget *parent)
    : QWidget(parent), m_iconLabel(new QLabel(this)), m_prefixLabel(new QLabel(this)), m_valueLabel(new QLabel(this)),
      m_unitLabel(new QLabel(this)), m_valueColor(K4Styles::Colors::AccentAmber) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_iconLabel);

    m_prefixLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_prefixLabel->setFont(K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeMedium, QFont::Bold));
    m_prefixLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextGray));
    m_prefixLabel->hide();
    layout->addWidget(m_prefixLabel);

    m_valueLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_valueLabel->setFont(K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(m_valueLabel);

    m_unitLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_unitLabel->setFont(K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeMedium, QFont::Normal));
    m_unitLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextGray));
    m_unitLabel->hide();
    layout->addWidget(m_unitLabel);

    applyValueStyle(m_valueColor);
    clear();
}

void IconTextLabel::setIcon(const QPixmap &pixmap) {
    if (pixmap.isNull()) {
        m_iconLabel->clear();
        return;
    }
    m_iconLabel->setPixmap(pixmap.scaled(kIconSize, kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void IconTextLabel::setLabel(const QString &label) {
    if (label.isEmpty()) {
        m_prefixLabel->clear();
        m_prefixLabel->hide();
        return;
    }
    m_prefixLabel->setText(label);
    m_prefixLabel->show();
}

void IconTextLabel::setValue(const QString &text) {
    if (text.isEmpty()) {
        clear();
        return;
    }
    m_valueLabel->setText(text);
    applyValueStyle(m_valueColor);
}

void IconTextLabel::setUnit(const QString &unit) {
    if (unit.isEmpty()) {
        m_unitLabel->clear();
        m_unitLabel->hide();
        return;
    }
    m_unitLabel->setText(unit);
    m_unitLabel->show();
}

void IconTextLabel::setValueColor(const QColor &color) {
    m_valueColor = color;
    applyValueStyle(m_valueColor);
}

void IconTextLabel::clear() {
    m_valueLabel->setText(kEmptyPlaceholder);
    // Placeholder reads as muted "no data" rather than active amber.
    applyValueStyle(QColor(K4Styles::Colors::TextFaded));
}

void IconTextLabel::applyValueStyle(const QColor &color) {
    m_valueLabel->setStyleSheet(QString("color: %1;").arg(color.name()));
}
