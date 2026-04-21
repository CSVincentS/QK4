#include "frequencyvfostate.h"

#include "models/radiostate.h"

void FrequencyVfoState::reset() {
    frequency = 0;
    vfoA = 0;
    vfoB = 0;
    splitEnabled = false;
    ritEnabled = false;
    xitEnabled = false;
    ritXitOffset = 0;
    ritEnabledB = false;
    ritXitOffsetB = 0;
}

namespace FrequencyVfoHandlers {

void handleFA(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && state.vfoA != freq) {
        state.vfoA = freq;
        state.frequency = freq;
        emit owner.frequencyChanged(freq);
    }
}

void handleFB(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && state.vfoB != freq) {
        state.vfoB = freq;
        emit owner.frequencyBChanged(freq);
    }
}

void handleFT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    const bool newSplit = (cmd.mid(2) == "1");
    if (newSplit != state.splitEnabled) {
        state.splitEnabled = newSplit;
        emit owner.splitChanged(state.splitEnabled);
    }
}

void handleRT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    const bool enabled = (cmd.at(2) == '1');
    if (enabled != state.ritEnabled) {
        state.ritEnabled = enabled;
        emit owner.ritXitChanged(state.ritEnabled, state.xitEnabled, state.ritXitOffset);
    }
}

void handleRTSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4 || (cmd.at(3) != '0' && cmd.at(3) != '1'))
        return;
    const bool enabled = (cmd.at(3) == '1');
    if (enabled != state.ritEnabledB) {
        state.ritEnabledB = enabled;
        emit owner.ritXitBChanged(state.ritEnabledB, state.ritXitOffsetB);
    }
}

void handleXT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    const bool enabled = (cmd.at(2) == '1');
    if (enabled != state.xitEnabled) {
        state.xitEnabled = enabled;
        emit owner.ritXitChanged(state.ritEnabled, state.xitEnabled, state.ritXitOffset);
    }
}

void handleRO(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int offset = cmd.mid(2).toInt(&ok);
    if (ok && offset != state.ritXitOffset) {
        state.ritXitOffset = offset;
        emit owner.ritXitChanged(state.ritEnabled, state.xitEnabled, state.ritXitOffset);
    }
}

void handleROSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    // WHY RO$ is per-VFO even though RT/XT are split across RO and RO$:
    //   - No split, RIT or XIT → offset in RO (VFO A)
    //   - Split + XIT          → offset in RO$ (VFO B = TX VFO)
    //   - BSET + RIT           → offset in RO$ (VFO B)
    // We always mutate ritXitOffsetB here and let the UI reassemble the
    // right display based on split/XIT/BSET state.
    if (cmd.length() < 4)
        return;
    bool ok;
    const int offset = cmd.mid(3).toInt(&ok);
    if (ok && offset != state.ritXitOffsetB) {
        state.ritXitOffsetB = offset;
        emit owner.ritXitBChanged(state.ritEnabledB, state.ritXitOffsetB);
    }
}

} // namespace FrequencyVfoHandlers
