#include "vforowindicatorcontroller.h"

#include "controllers/spectrumcontroller.h"
#include "models/radiostate.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/vforowwidget.h"

#include <QLabel>
#include <QString>

namespace {

QString largeBoldColor(const char *color) {
    return QString("color: %1; font-size: %2px; font-weight: bold;")
        .arg(color)
        .arg(K4Styles::Dimensions::FontSizeLarge);
}

} // namespace

VfoRowIndicatorController::VfoRowIndicatorController(RadioState *radioState, SpectrumController *spectrum,
                                                     VfoRowWidget *vfoRow, const Labels &labels, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_spectrum(spectrum), m_vfoRow(vfoRow), m_labels(labels) {
    connect(m_radioState, &RadioState::splitChanged, this, &VfoRowIndicatorController::onSplitChanged);
    connect(m_radioState, &RadioState::voxChanged, this, &VfoRowIndicatorController::onVoxChanged);
    // WHY: VOX state is per-mode-class (VXC/VXV/VXD). When the mode flips,
    // the displayed VOX color needs to re-resolve from the new mode's VOX
    // flag even though voxChanged itself didn't fire.
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { onVoxChanged(); });
    connect(m_radioState, &RadioState::qskEnabledChanged, this, &VfoRowIndicatorController::onQskEnabledChanged);
    connect(m_radioState, &RadioState::testModeChanged, this, &VfoRowIndicatorController::onTestModeChanged);
    connect(m_radioState, &RadioState::atuModeChanged, this, &VfoRowIndicatorController::onAtuModeChanged);
    connect(m_radioState, &RadioState::messageBankChanged, this, &VfoRowIndicatorController::onMessageBankChanged);
}

VfoRowIndicatorController::~VfoRowIndicatorController() {
    disconnect(this);
}

void VfoRowIndicatorController::onSplitChanged(bool enabled) {
    const QString splitStyle = QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::AccentAmber)
                                   .arg(K4Styles::Dimensions::FontSizeButton);
    m_labels.splitLabel->setStyleSheet(splitStyle);
    m_labels.splitLabel->setText(enabled ? "SPLIT ON" : "SPLIT OFF");

    // TX triangle follows which VFO transmits.
    if (enabled) {
        m_labels.txTriangle->setText("");
        m_labels.txTriangleB->setText("▶");
    } else {
        m_labels.txTriangle->setText("◀");
        m_labels.txTriangleB->setText("");
    }
    // Split flip moves the TX VFO — refresh TX marker position.
    m_spectrum->updateTxMarkers();
}

void VfoRowIndicatorController::onVoxChanged() {
    // WHY: K4 has per-mode-class VOX state (VXC/VXV/VXD for CW/voice/data);
    // the generic voxChanged signal fires on any of them. Ask RadioState
    // for "is VOX on for the currently-tuned mode" and color accordingly.
    const bool voxOn = m_radioState->voxForCurrentMode();
    m_labels.voxLabel->setStyleSheet(
        largeBoldColor(voxOn ? K4Styles::Colors::AccentAmber : K4Styles::Colors::TextGray));
}

void VfoRowIndicatorController::onQskEnabledChanged(bool enabled) {
    m_labels.qskLabel->setStyleSheet(
        largeBoldColor(enabled ? K4Styles::Colors::TextWhite : K4Styles::Colors::TextGray));
}

void VfoRowIndicatorController::onTestModeChanged(bool enabled) {
    m_vfoRow->setTestVisible(enabled);
}

void VfoRowIndicatorController::onAtuModeChanged(int mode) {
    // ATU mode 2 = AUTO; anything else renders greyed.
    m_labels.atuLabel->setStyleSheet(
        largeBoldColor(mode == 2 ? K4Styles::Colors::AccentAmber : K4Styles::Colors::TextGray));
}

void VfoRowIndicatorController::onMessageBankChanged(int bank) {
    m_labels.msgBankLabel->setText(bank == 1 ? "MSG: I" : "MSG: II");
}
