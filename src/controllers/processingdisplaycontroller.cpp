#include "processingdisplaycontroller.h"

#include "models/radiostate.h"
#include "ui/widgets/vfowidget.h"

#include <QString>

namespace {

QString agcText(RadioState::AGCSpeed speed) {
    switch (speed) {
    case RadioState::AGC_Off:
        return QStringLiteral("AGC");
    case RadioState::AGC_Slow:
        return QStringLiteral("AGC-S");
    case RadioState::AGC_Fast:
        return QStringLiteral("AGC-F");
    }
    return QStringLiteral("AGC");
}

} // namespace

ProcessingDisplayController::ProcessingDisplayController(RadioState *radioState, VFOWidget *vfoA, VFOWidget *vfoB,
                                                         QObject *parent)
    : QObject(parent), m_radioState(radioState), m_vfoA(vfoA), m_vfoB(vfoB) {
    connect(m_radioState, &RadioState::processingChanged, this, &ProcessingDisplayController::refreshVfoA);
    connect(m_radioState, &RadioState::processingChangedB, this, &ProcessingDisplayController::refreshVfoB);
}

ProcessingDisplayController::~ProcessingDisplayController() {
    disconnect(this);
}

void ProcessingDisplayController::refreshVfoA() {
    m_vfoA->setAGC(agcText(m_radioState->agcSpeed()));
    m_vfoA->setPreamp(m_radioState->preampEnabled() && m_radioState->preamp() > 0, m_radioState->preamp());
    m_vfoA->setAtt(m_radioState->attenuatorEnabled() && m_radioState->attenuatorLevel() > 0,
                   m_radioState->attenuatorLevel());
    m_vfoA->setNB(m_radioState->noiseBlankerEnabled());
    m_vfoA->setNR(m_radioState->noiseReductionEnabled(), m_radioState->ssnrEnabled());
}

void ProcessingDisplayController::refreshVfoB() {
    m_vfoB->setAGC(agcText(m_radioState->agcSpeedB()));
    m_vfoB->setPreamp(m_radioState->preampEnabledB() && m_radioState->preampB() > 0, m_radioState->preampB());
    m_vfoB->setAtt(m_radioState->attenuatorEnabledB() && m_radioState->attenuatorLevelB() > 0,
                   m_radioState->attenuatorLevelB());
    m_vfoB->setNB(m_radioState->noiseBlankerEnabledB());
    m_vfoB->setNR(m_radioState->noiseReductionEnabledB(), m_radioState->ssnrEnabledB());
}
