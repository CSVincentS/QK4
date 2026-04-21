#include "antennadisplaycontroller.h"

#include "models/radiostate.h"

#include <QLabel>
#include <QString>

namespace {

// Format Main RX antenna display based on AR command value.
// K4 AR command values (from official K4 protocol documentation):
//   0 = Disconnected (all RX RF sources disconnected)
//   1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
//   2 = RX USES TX ANT (follows TX antenna selection) — show resolved value
//   3 = INT. XVTR IN (internal transverter)
//   4 = RX ANT IN1 (receive antenna jack)
//   5 = ATU RX ANT1 (TX antenna 1 via ATU)
//   6 = ATU RX ANT2 (TX antenna 2 via ATU)
//   7 = ATU RX ANT3 (TX antenna 3 via ATU)
QString formatMainRxAntenna(int arValue, int txAnt, RadioState *radioState) {
    switch (arValue) {
    case 0:
        return QStringLiteral("OFF");
    case 1:
        return QString("RX2:%1").arg(radioState->antennaName(5));
    case 2: // Resolve to current TX antenna (matches K4 front-panel behavior).
        return QString("%1:%2").arg(txAnt).arg(radioState->antennaName(txAnt));
    case 3:
        return QStringLiteral("INT XVTR");
    case 4:
        return QString("RX1:%1").arg(radioState->antennaName(4));
    case 5:
        return QString("1:%1").arg(radioState->antennaName(1));
    case 6:
        return QString("2:%1").arg(radioState->antennaName(2));
    case 7:
        return QString("3:%1").arg(radioState->antennaName(3));
    default:
        return QString("AR%1").arg(arValue);
    }
}

// Format Sub RX antenna display based on AR$ command value. Same semantics
// as AR — listed here so the unknown-value fallback shows "AR$" (not "AR").
QString formatSubRxAntenna(int arValue, int txAnt, RadioState *radioState) {
    switch (arValue) {
    case 0:
        return QStringLiteral("OFF");
    case 1:
        return QString("RX2:%1").arg(radioState->antennaName(5));
    case 2:
        return QString("%1:%2").arg(txAnt).arg(radioState->antennaName(txAnt));
    case 3:
        return QStringLiteral("INT XVTR");
    case 4:
        return QString("RX1:%1").arg(radioState->antennaName(4));
    case 5:
        return QString("1:%1").arg(radioState->antennaName(1));
    case 6:
        return QString("2:%1").arg(radioState->antennaName(2));
    case 7:
        return QString("3:%1").arg(radioState->antennaName(3));
    default:
        return QString("AR$%1").arg(arValue);
    }
}

} // namespace

AntennaDisplayController::AntennaDisplayController(RadioState *radioState, QLabel *txAntennaLabel, QLabel *rxAntALabel,
                                                   QLabel *rxAntBLabel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_txAntennaLabel(txAntennaLabel), m_rxAntALabel(rxAntALabel),
      m_rxAntBLabel(rxAntBLabel) {
    connect(m_radioState, &RadioState::antennaChanged, this, &AntennaDisplayController::onAntennaChanged);
    // Antenna name changes don't include AR/AR$ values; re-read RadioState and reformat.
    connect(m_radioState, &RadioState::antennaNameChanged, this, [this](int, const QString &) { refreshLabels(); });
}

AntennaDisplayController::~AntennaDisplayController() {
    disconnect(this);
}

void AntennaDisplayController::onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub) {
    // TX antenna (AN command) — always 1-3, format as "N:name".
    m_txAntennaLabel->setText(QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt)));
    m_rxAntALabel->setText(formatMainRxAntenna(rxAntMain, txAnt, m_radioState));
    m_rxAntBLabel->setText(formatSubRxAntenna(rxAntSub, txAnt, m_radioState));
}

void AntennaDisplayController::refreshLabels() {
    onAntennaChanged(m_radioState->txAntenna(), m_radioState->rxAntennaMain(), m_radioState->rxAntennaSub());
}

void AntennaDisplayController::reset() {
    m_txAntennaLabel->setText(QString());
    m_rxAntALabel->setText(QString());
    m_rxAntBLabel->setText(QString());
}
