#include "qskcontrolstate.h"

#include "models/radiostate.h"

void QskControlState::reset() {
    qskEnabled = false;
    qskDelayCW = -1;
    qskDelayVoice = -1;
    qskDelayData = -1;
}

namespace QskHandlers {

void handleSD(QskControlState &state, RadioState &owner, const QString &cmd) {
    // SDxMzzz where x=QSK flag, M=mode char (C/V/D), zzz=delay in 10ms increments.
    if (cmd.length() < 7)
        return;
    const QChar qskFlag = cmd.at(2);
    const QChar modeChar = cmd.at(3);
    bool ok;
    const int delay = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;

    // QSK enable flag is only meaningful on the CW-mode SD echo.
    if (modeChar == 'C') {
        const bool qskOn = (qskFlag == '1');
        if (qskOn != state.qskEnabled) {
            state.qskEnabled = qskOn;
            emit owner.qskEnabledChanged(state.qskEnabled);
        }
    }

    // WHY: only emit qskDelayChanged when the updated bucket matches the
    // current operating mode, so UI consumers don't refresh for buckets
    // the user can't see.
    bool isCurrentMode = false;
    const RadioState::Mode current = owner.mode();
    if (modeChar == 'C') {
        if (state.qskDelayCW != delay) {
            state.qskDelayCW = delay;
            isCurrentMode = (current == RadioState::CW || current == RadioState::CW_R);
        }
    } else if (modeChar == 'V') {
        if (state.qskDelayVoice != delay) {
            state.qskDelayVoice = delay;
            isCurrentMode = (current == RadioState::LSB || current == RadioState::USB || current == RadioState::AM ||
                             current == RadioState::FM);
        }
    } else if (modeChar == 'D') {
        if (state.qskDelayData != delay) {
            state.qskDelayData = delay;
            isCurrentMode = (current == RadioState::DATA || current == RadioState::DATA_R);
        }
    }
    if (isCurrentMode)
        emit owner.qskDelayChanged(delay);
}

} // namespace QskHandlers
