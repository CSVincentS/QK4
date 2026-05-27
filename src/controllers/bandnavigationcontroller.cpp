#include "bandnavigationcontroller.h"

#include "connectioncontroller.h"
#include "controllers/popupmanager.h"
#include "models/radiostate.h"

#include <QChar>
#include <QLoggingCategory>
#include <QString>

Q_LOGGING_CATEGORY(qk4BandNav, "qk4.bandnav")

BandNavigationController::BandNavigationController(RadioState *radioState, ConnectionController *connection,
                                                   PopupManager *popupManager, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_popupManager(popupManager) {
    connect(m_popupManager, &PopupManager::bandSelected, this, &BandNavigationController::onBandSelected);
    connect(m_connection, &ConnectionController::catResponseReceived, this, &BandNavigationController::onCatResponse);
}

void BandNavigationController::onCatResponse(const QString &response) {
    // Each ;-delimited piece is a single CAT command. Parse BN / BN$
    // echoes and ignore everything else — RadioState handles the rest.
    const QStringList commands = response.split(';', Qt::SkipEmptyParts);
    for (const QString &cmd : commands) {
        if (cmd.startsWith("BN$")) {
            bool ok;
            const int bandNum = cmd.mid(3, 2).toInt(&ok);
            if (ok)
                setCurrentBand(bandNum, /*forVfoB=*/true);
        } else if (cmd.startsWith("BN")) {
            bool ok;
            const int bandNum = cmd.mid(2, 2).toInt(&ok);
            if (ok)
                setCurrentBand(bandNum, /*forVfoB=*/false);
        }
    }
}

BandNavigationController::~BandNavigationController() {
    disconnect(this);
}

void BandNavigationController::setCurrentBand(int bandNum, bool forVfoB) {
    const int previous = forVfoB ? m_currentBandNumB : m_currentBandNum;
    if (forVfoB) {
        m_currentBandNumB = bandNum;
        // Only refresh the popup indicator when BSet is active — that's
        // when the popup is showing VFO B's band state.
        if (m_radioState->bSetEnabled())
            m_popupManager->setSelectedBandByNumber(bandNum);
    } else {
        m_currentBandNum = bandNum;
        if (!m_radioState->bSetEnabled())
            m_popupManager->setSelectedBandByNumber(bandNum);
    }
    if (previous != bandNum)
        emit currentBandChanged(bandNum, forVfoB);
}

void BandNavigationController::onBandSelected(const QString &bandName) {
    qCDebug(qk4BandNav) << "Band selected:" << bandName;

    const int newBandNum = m_popupManager->bandNumberForName(bandName);

    // GEN and MEM are special pseudo-bands — no BN command is sent.
    if (newBandNum < 0) {
        qCDebug(qk4BandNav) << "Special mode selected (GEN/MEM) — no BN command";
        return;
    }

    if (!m_connection->isConnected())
        return;

    const bool bSetEnabled = m_radioState->bSetEnabled();
    const int currentBand = bSetEnabled ? m_currentBandNumB : m_currentBandNum;
    const QString cmdPrefix = bSetEnabled ? "BN$" : "BN";

    if (newBandNum == currentBand) {
        // Tap on the already-selected band → K4 band-stack step.
        const QString bandStackCmd = bSetEnabled ? "BN$^;" : "BN^;";
        qCDebug(qk4BandNav) << "Same band — invoking band stack with" << bandStackCmd;
        m_connection->sendCAT(bandStackCmd);
    } else {
        const QString cmd = QString("%1%2;").arg(cmdPrefix).arg(newBandNum, 2, 10, QChar('0'));
        qCDebug(qk4BandNav) << "Changing band:" << cmd;
        m_connection->sendCAT(cmd);
    }
    // Request a fresh echo so our cached current-band state updates.
    m_connection->sendCAT(bSetEnabled ? "BN$;" : "BN;");
}
