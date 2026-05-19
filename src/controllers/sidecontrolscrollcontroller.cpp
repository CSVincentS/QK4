#include "sidecontrolscrollcontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "ui/widgets/sidecontrolpanel.h"

#include <QChar>
#include <QString>
#include <QtGlobal>

SideControlScrollController::SideControlScrollController(RadioState *radioState, ConnectionController *connection,
                                                         SideControlPanel *panel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_panel(panel) {
    // Connect side control panel scroll signals to CAT commands.
    // After sending CAT, update RadioState optimistically — the K4 doesn't
    // echo most of these SET commands.

    // Group 1: WPM/PTCH (CW mode) and MIC/CMP (Voice mode)
    connect(m_panel, &SideControlPanel::wpmChanged, this, [this](int delta) {
        int newWpm = qBound(8, m_radioState->keyerSpeed() + delta, 50);
        m_connection->sendCAT(QString("KS%1;").arg(newWpm, 3, 10, QChar('0')));
        m_radioState->setKeyerSpeed(newWpm);
    });
    connect(m_panel, &SideControlPanel::pitchChanged, this, [this](int delta) {
        int currentPitch = m_radioState->cwPitch(); // In Hz
        int newPitch = qBound(300, currentPitch + (delta * 10), 990);
        m_connection->sendCAT(QString("CW%1;").arg(newPitch / 10, 2, 10, QChar('0')));
        m_radioState->setCwPitch(newPitch);
    });
    connect(m_panel, &SideControlPanel::micGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->micGain() + delta, 80);
        m_connection->sendCAT(QString("MG%1;").arg(newGain, 3, 10, QChar('0')));
        m_radioState->setMicGain(newGain);
    });
    connect(m_panel, &SideControlPanel::compressionChanged, this, [this](int delta) {
        int newComp = qBound(0, m_radioState->compression() + delta, 30);
        m_connection->sendCAT(QString("CP%1;").arg(newComp, 3, 10, QChar('0')));
        m_radioState->setCompression(newComp);
    });

    // Group 1: PWR/DLY
    // PC command uses PCnnnr; format: L=QRP (0.1-10W), H=QRO (11-110W)
    // QRP (≤10W): 0.1W increments, e.g., 10.0, 9.9, 9.8, ... 0.1
    // QRO (>10W): 1W increments, e.g., 11, 12, 13, ... 110
    connect(m_panel, &SideControlPanel::powerChanged, this, [this](int delta) {
        double currentPower = m_radioState->rfPower();
        double newPower;

        if (currentPower <= 10.0) {
            // Currently in QRP range: 0.1W increments
            newPower = currentPower + (delta * 0.1);
            if (newPower > 10.0) {
                // Transition to QRO at 11W
                newPower = 11.0;
                int powerVal = static_cast<int>(newPower);
                m_connection->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(0.1, newPower, 10.0);
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 9.9W = 099
                m_connection->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            }
        } else {
            // Currently in QRO range: 1W increments
            newPower = currentPower + delta;
            if (newPower <= 10.0) {
                // Transition to QRP at 10.0W
                newPower = 10.0;
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 10.0W = 100
                m_connection->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(11.0, newPower, 110.0);
                int powerVal = static_cast<int>(newPower);
                m_connection->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            }
        }
        m_radioState->setRfPower(newPower);
    });

    connect(m_panel, &SideControlPanel::delayChanged, this, [this](int delta) {
        int currentDelay = m_radioState->delayForCurrentMode();
        if (currentDelay < 0)
            currentDelay = 0;                                // Handle uninitialized
        int newDelay = qBound(0, currentDelay + delta, 255); // 0-255 = 0.00 to 2.55 seconds

        // Optimistic update - update local state immediately
        m_radioState->setDelayForCurrentMode(newDelay);

        // SD command format: SDxyzzz where x=QSK flag, y=mode (C/V/D), zzz=delay in 10ms
        // Determine mode character based on current operating mode
        QChar modeChar = 'V'; // Default to Voice
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            modeChar = 'C';
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            modeChar = 'D';
        }
        // x=0 means use specified delay (not full QSK)
        m_connection->sendCAT(QString("SD0%1%2;").arg(modeChar).arg(newDelay, 3, 10, QChar('0')));
    });

    // Group 2: BW/HI and SHFT/LO
    // BW command uses 10Hz units (divide by 10)
    connect(m_panel, &SideControlPanel::bandwidthChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        int currentBw = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();

        // Mode-specific BW limits (Hz)
        int bwMin = 50, bwMax = 5000;
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D
                bwMin = 150;
                bwMax = 800;
            } else if (subMode == 3) { // PSK-D
                bwMax = 200;
            }
        }

        int newBw = qBound(bwMin, currentBw + (delta * 50), bwMax);
        QString cmd = bSet ? "BW$" : "BW";
        m_connection->sendCAT(QString("%1%2;").arg(cmd).arg(newBw / 10, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setFilterBandwidthB(newBw);
        } else {
            m_radioState->setFilterBandwidth(newBw);
        }
    });

    connect(m_panel, &SideControlPanel::highCutChanged, this,
            [this](int delta) { applyFilterEdgeChange(/*adjustHi=*/true, delta); });

    connect(m_panel, &SideControlPanel::shiftChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();

        // IS is locked in certain modes — ignore scroll
        if (mode == RadioState::AM || mode == RadioState::FM)
            return;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2 || subMode == 3)
                return; // FSK-D, PSK-D: IS locked
        }

        int currentShift = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
        int isMax = (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300;
        int newShift = qBound(30, currentShift + delta, isMax);
        QString prefix = bSet ? "IS$" : "IS";
        m_connection->sendCAT(QString("%1+%2;").arg(prefix).arg(newShift, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setIfShiftB(newShift);
        } else {
            m_radioState->setIfShift(newShift);
        }
    });

    connect(m_panel, &SideControlPanel::lowCutChanged, this,
            [this](int delta) { applyFilterEdgeChange(/*adjustHi=*/false, delta); });

    // Group 3: M.RF/M.SQL and S.RF/S.SQL
    // RF Gain uses RG-nn; format where nn is 00-60 (representing -0 to -60 dB attenuation)
    // Scroll up = less attenuation = decrease value, scroll down = more attenuation = increase value
    connect(m_panel, &SideControlPanel::mainRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGain() - delta, 60);
        m_connection->sendCAT(QString("RG-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGain(newGain);
    });
    connect(m_panel, &SideControlPanel::mainSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevel() + delta, 29);
        m_connection->sendCAT(QString("SQ%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevel(newSql);
    });
    connect(m_panel, &SideControlPanel::subRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGainB() - delta, 60);
        m_connection->sendCAT(QString("RG$-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGainB(newGain);
    });
    connect(m_panel, &SideControlPanel::subSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevelB() + delta, 29);
        m_connection->sendCAT(QString("SQ$%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevelB(newSql);
    });
}

SideControlScrollController::~SideControlScrollController() {
    disconnect(this);
}

void SideControlScrollController::applyFilterEdgeChange(bool adjustHi, int delta) {
    // HI and LO adjust filter edges while keeping the OPPOSITE edge fixed.
    // Both BW and IS must change. Work in decahertz (dah) to avoid rounding
    // drift. Step is 2 dah (20 Hz) per scroll tick — keeps IS on-grid.
    bool bSet = m_radioState->bSetEnabled();
    RadioState::Mode mode = m_radioState->mode();
    int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
    int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

    // Mode-specific BW limits (dah) and IS-locked flag
    int bwMinDah = 5, bwMaxDah = 500;
    bool isLocked = false;
    if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
        int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
        if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
            bwMinDah = 15;
            bwMaxDah = 80;
            isLocked = true;
        } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
            bwMaxDah = 20;
            isLocked = true;
        }
    }

    // Compute displayed (clamped) HI/LO from current BW + IS
    int loDah = qMax(0, isDah - bwDah / 2);
    int hiDah = loDah + bwDah;

    int newBwDah;
    int newHiDah;
    int newLoDah;
    if (adjustHi) {
        newHiDah = hiDah + (delta * 2);
        if (newHiDah <= loDah)
            return;
        newLoDah = loDah;
        newBwDah = qBound(bwMinDah, newHiDah - loDah, bwMaxDah);
    } else {
        newLoDah = loDah + (delta * 2);
        if (newLoDah >= hiDah)
            return;
        newHiDah = hiDah;
        newBwDah = qBound(bwMinDah, hiDah - newLoDah, bwMaxDah);
    }

    QString bwCmd = bSet ? "BW$" : "BW";
    m_connection->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

    if (isLocked) {
        // IS stays fixed — only BW changes
        if (bSet)
            m_radioState->setFilterBandwidthB(newBwDah * 10);
        else
            m_radioState->setFilterBandwidth(newBwDah * 10);
    } else {
        int newIsDah =
            qBound(30, (newHiDah + newLoDah) / 2, (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300);
        QString isPrefix = bSet ? "IS$" : "IS";
        m_connection->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

        if (bSet) {
            m_radioState->setFilterBandwidthB(newBwDah * 10);
            m_radioState->setIfShiftB(newIsDah);
        } else {
            m_radioState->setFilterBandwidth(newBwDah * 10);
            m_radioState->setIfShift(newIsDah);
        }
    }
}
