#include "txstatecontroller.h"

#include "controllers/statusbarcontroller.h"
#include "controllers/vfofrequencycontroller.h"
#include "models/radiostate.h"
#include "ui/k4styles.h"
#include "ui/sidecontrolpanel.h"
#include "ui/vfowidget.h"

#include <QLabel>
#include <QString>

TxStateController::TxStateController(RadioState *radioState, StatusBarController *statusBar,
                                     SideControlPanel *sideControlPanel, VfoFrequencyController *vfoFrequencyController,
                                     VFOWidget *vfoA, VFOWidget *vfoB, QLabel *txIndicator, QLabel *txTriangle,
                                     QLabel *txTriangleB, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_statusBar(statusBar), m_sideControlPanel(sideControlPanel),
      m_vfoFrequencyController(vfoFrequencyController), m_vfoA(vfoA), m_vfoB(vfoB), m_txIndicator(txIndicator),
      m_txTriangle(txTriangle), m_txTriangleB(txTriangleB) {
    connect(m_radioState, &RadioState::transmitStateChanged, this, &TxStateController::onTransmitStateChanged);
    connect(m_radioState, &RadioState::txMeterChanged, this, &TxStateController::onTxMeterChanged);
}

TxStateController::~TxStateController() {
    disconnect(this);
}

void TxStateController::onTransmitStateChanged(bool transmitting) {
    // Only the active TX VFO flips its meter to TX mode; the other VFO
    // stays on S-meter so you can see incoming signal on the non-TX side.
    if (m_radioState->splitEnabled()) {
        m_vfoA->setTransmitting(false);
        m_vfoB->setTransmitting(transmitting);
    } else {
        m_vfoA->setTransmitting(transmitting);
        m_vfoB->setTransmitting(false);
    }

    const QString color = transmitting ? K4Styles::Colors::TxRed : K4Styles::Colors::AccentAmber;
    const QString indicatorStyle = QString("color: %1; font-size: %2px; font-weight: bold;")
                                       .arg(color)
                                       .arg(K4Styles::Dimensions::FontSizeIndicator);
    const QString triangleStyle =
        QString("color: %1; font-size: %2px;").arg(color).arg(K4Styles::Dimensions::FontSizeIndicator);
    m_txIndicator->setStyleSheet(indicatorStyle);
    m_txTriangle->setStyleSheet(triangleStyle);
    m_txTriangleB->setStyleSheet(triangleStyle);

    // When XIT is on, rendered VFO frequency = dial + XIT offset; force a
    // refresh on the TX VFO so the display matches the actual TX freq
    // when transmitting, and restores to the RX freq on return to RX.
    if (m_radioState->xitEnabled()) {
        if (m_radioState->splitEnabled())
            m_vfoFrequencyController->refreshVfoB();
        else
            m_vfoFrequencyController->refreshVfoA();
    }
}

void TxStateController::onTxMeterChanged(int alc, int comp, double fwdPower, double swr) {
    m_statusBar->setForwardPower(fwdPower);
    m_sideControlPanel->setPowerReading(fwdPower);

    // WHY: K4's CAT protocol exposes forward power but not PA drain current.
    // Id = P / (V × η). Measured K4 efficiency is ~34% (80W @ 17A @ 13.8V).
    // Gives a workable "Id" readout on the VFO multifunction meter without
    // an extra CAT poll.
    const double voltage = m_radioState->supplyVoltage();
    double paCurrent = 0.0;
    if (voltage > 0 && fwdPower > 0) {
        constexpr double K4_PA_EFFICIENCY = 0.34;
        paCurrent = fwdPower / (voltage * K4_PA_EFFICIENCY);
    }

    // Route meter tuple + current to the active TX VFO only.
    if (m_radioState->splitEnabled()) {
        m_vfoB->setTxMeters(alc, comp, fwdPower, swr);
        m_vfoB->setTxMeterCurrent(paCurrent);
    } else {
        m_vfoA->setTxMeters(alc, comp, fwdPower, swr);
        m_vfoA->setTxMeterCurrent(paCurrent);
    }
}
