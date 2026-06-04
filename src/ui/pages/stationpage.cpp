#include "ui/pages/stationpage.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

StationPage::StationPage(QWidget *parent) : QWidget(parent) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *titleLabel = new QLabel("Station", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    RadioSettings *settings = RadioSettings::instance();

    // Form grid: label on the left, field on the right.
    auto *form = new QGridLayout();
    form->setHorizontalSpacing(K4Styles::Dimensions::PaddingMedium);
    form->setVerticalSpacing(K4Styles::Dimensions::PaddingMedium);
    int row = 0;

    auto addField = [&](const QString &labelText, QLineEdit *edit, const QString &placeholder) {
        auto *lbl = new QLabel(labelText, this);
        lbl->setStyleSheet(K4Styles::Dialog::formLabel());
        edit->setStyleSheet(K4Styles::Dialog::lineEdit());
        edit->setPlaceholderText(placeholder);
        form->addWidget(lbl, row, 0);
        form->addWidget(edit, row, 1);
        row++;
    };

    m_callSignEdit = new QLineEdit(this);
    m_callSignEdit->setText(settings->callSign());
    addField("Call Sign", m_callSignEdit, "e.g. KF5O");

    m_operatorNameEdit = new QLineEdit(this);
    m_operatorNameEdit->setText(settings->operatorName());
    addField("Operator Name", m_operatorNameEdit, "Your name");

    m_gridSquareEdit = new QLineEdit(this);
    m_gridSquareEdit->setText(settings->gridSquare());
    addField("Grid Square", m_gridSquareEdit, "e.g. EM12");

    m_qthEdit = new QLineEdit(this);
    m_qthEdit->setText(settings->qth());
    addField("QTH / Location", m_qthEdit, "City / location");

    // IARU Region dropdown — drives the band-plan overlay.
    auto *regionLabel = new QLabel("IARU Region", this);
    regionLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_regionCombo = new QComboBox(this);
    m_regionCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_regionCombo->addItem("Region 1 — Europe / Africa", 1);
    m_regionCombo->addItem("Region 2 — The Americas", 2);
    m_regionCombo->addItem("Region 3 — Asia-Pacific", 3);
    m_regionCombo->addItem("United States (FCC)", 4);
    int regionIdx = m_regionCombo->findData(settings->iaruRegion());
    m_regionCombo->setCurrentIndex(regionIdx >= 0 ? regionIdx : 1);
    form->addWidget(regionLabel, row, 0);
    form->addWidget(m_regionCombo, row, 1);
    row++;

    layout->addLayout(form);

    // Band-plan overlay toggle.
    m_bandPlanCheck = new QCheckBox("Show band-plan overlay on spectrum", this);
    m_bandPlanCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    m_bandPlanCheck->setChecked(settings->bandPlanOverlayEnabled());
    layout->addWidget(m_bandPlanCheck);

    auto *help = new QLabel("Sets the band-plan strip at the top of the spectrum. IARU regions are voluntary "
                            "recommendations; \"United States (FCC)\" uses practical Extra-class mode boundaries "
                            "plus FT8/FT4/WSPR markers. Your national regulations and license class take precedence.",
                            this);
    help->setWordWrap(true);
    help->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(help);

    layout->addStretch();

    // Persist on edit. Text fields commit on editingFinished (focus-out / Enter) to avoid
    // re-saving on every keystroke.
    connect(m_regionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &StationPage::onIaruRegionChanged);
    connect(m_callSignEdit, &QLineEdit::editingFinished, this, &StationPage::onCallSignEdited);
    connect(m_gridSquareEdit, &QLineEdit::editingFinished, this, &StationPage::onGridSquareEdited);
    connect(m_operatorNameEdit, &QLineEdit::editingFinished, this, &StationPage::onOperatorNameEdited);
    connect(m_qthEdit, &QLineEdit::editingFinished, this, &StationPage::onQthEdited);
    connect(m_bandPlanCheck, &QCheckBox::toggled, this, &StationPage::onBandPlanOverlayToggled);
}

void StationPage::onIaruRegionChanged(int index) {
    if (index < 0)
        return;
    RadioSettings::instance()->setIaruRegion(m_regionCombo->itemData(index).toInt());
}

void StationPage::onCallSignEdited() {
    RadioSettings::instance()->setCallSign(m_callSignEdit->text());
    // Reflect the normalized (upper-cased/trimmed) value back into the field.
    m_callSignEdit->setText(RadioSettings::instance()->callSign());
}

void StationPage::onGridSquareEdited() {
    RadioSettings::instance()->setGridSquare(m_gridSquareEdit->text());
}

void StationPage::onOperatorNameEdited() {
    RadioSettings::instance()->setOperatorName(m_operatorNameEdit->text());
}

void StationPage::onQthEdited() {
    RadioSettings::instance()->setQth(m_qthEdit->text());
}

void StationPage::onBandPlanOverlayToggled(bool checked) {
    RadioSettings::instance()->setBandPlanOverlayEnabled(checked);
}
