#include "processingstate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void ProcessingState::reset() {
    // Main RX
    noiseBlankerLevel = 0;
    noiseBlankerEnabled = false;
    noiseBlankerFilterWidth = 0;
    noiseReductionLevel = 0;
    noiseReductionEnabled = false;
    autoNotchEnabled = false;
    manualNotchEnabled = false;
    manualNotchPitch = 1000;
    preamp = 0;
    preampEnabled = false;
    attenuatorLevel = 0;
    attenuatorEnabled = false;
    agcSpeed = RadioState::AGC_Slow;

    // Sub RX
    noiseBlankerLevelB = 0;
    noiseBlankerEnabledB = false;
    noiseBlankerFilterWidthB = 0;
    noiseReductionLevelB = 0;
    noiseReductionEnabledB = false;
    autoNotchEnabledB = false;
    manualNotchEnabledB = false;
    manualNotchPitchB = 1000;
    preampB = 0;
    preampEnabledB = false;
    attenuatorLevelB = 0;
    attenuatorEnabledB = false;
    agcSpeedB = RadioState::AGC_Slow;
}

namespace ProcessingHandlers {

void handleNB(ProcessingState &state, RadioState &owner, const QString &cmd) {
    // NBnnm or NBnnmf : nn=level 00-15, m=on/off, f=filter 0/1/2
    if (cmd.length() < 4)
        return;
    const QString nbStr = cmd.mid(2);
    if (nbStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = nbStr.left(2).toInt(&ok1);
    const int enabled = nbStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const int newLevel = qMin(level, 15);
    const bool newEnabled = (enabled == 1);
    int newFilter = state.noiseBlankerFilterWidth;
    if (nbStr.length() >= 4) {
        bool ok3;
        const int filter = nbStr.mid(3, 1).toInt(&ok3);
        if (ok3)
            newFilter = qMin(filter, 2);
    }
    if (newLevel == state.noiseBlankerLevel && newEnabled == state.noiseBlankerEnabled &&
        newFilter == state.noiseBlankerFilterWidth)
        return;
    state.noiseBlankerLevel = newLevel;
    state.noiseBlankerEnabled = newEnabled;
    state.noiseBlankerFilterWidth = newFilter;
    emit owner.processingChanged();
}

void handleNBSub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    const QString nbStr = cmd.mid(3);
    if (nbStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = nbStr.left(2).toInt(&ok1);
    const int enabled = nbStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const int newLevel = qMin(level, 15);
    const bool newEnabled = (enabled == 1);
    int newFilter = state.noiseBlankerFilterWidthB;
    if (nbStr.length() >= 4) {
        bool ok3;
        const int filter = nbStr.mid(3, 1).toInt(&ok3);
        if (ok3)
            newFilter = qMin(filter, 2);
    }
    if (newLevel == state.noiseBlankerLevelB && newEnabled == state.noiseBlankerEnabledB &&
        newFilter == state.noiseBlankerFilterWidthB)
        return;
    state.noiseBlankerLevelB = newLevel;
    state.noiseBlankerEnabledB = newEnabled;
    state.noiseBlankerFilterWidthB = newFilter;
    emit owner.processingChangedB();
}

void handleNR(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    const QString nrStr = cmd.mid(2);
    if (nrStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = nrStr.left(2).toInt(&ok1);
    const int enabled = nrStr.right(1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.noiseReductionLevel && newEnabled == state.noiseReductionEnabled)
        return;
    state.noiseReductionLevel = level;
    state.noiseReductionEnabled = newEnabled;
    emit owner.processingChanged();
}

void handleNRSub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    const QString nrStr = cmd.mid(3);
    if (nrStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = nrStr.left(2).toInt(&ok1);
    const int enabled = nrStr.right(1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.noiseReductionLevelB && newEnabled == state.noiseReductionEnabledB)
        return;
    state.noiseReductionLevelB = level;
    state.noiseReductionEnabledB = newEnabled;
    emit owner.processingChangedB();
}

void handlePA(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    const QString paStr = cmd.mid(2);
    if (paStr.length() < 2)
        return;
    bool ok1, ok2;
    const int level = paStr.left(1).toInt(&ok1);
    const int enabled = paStr.mid(1, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.preamp && newEnabled == state.preampEnabled)
        return;
    state.preamp = level;
    state.preampEnabled = newEnabled;
    emit owner.processingChanged();
}

void handlePASub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    const QString paStr = cmd.mid(3);
    if (paStr.length() < 2)
        return;
    bool ok1, ok2;
    const int level = paStr.left(1).toInt(&ok1);
    const int enabled = paStr.mid(1, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.preampB && newEnabled == state.preampEnabledB)
        return;
    state.preampB = level;
    state.preampEnabledB = newEnabled;
    emit owner.processingChangedB();
}

void handleRA(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    const QString raStr = cmd.mid(2);
    if (raStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = raStr.left(2).toInt(&ok1);
    const int enabled = raStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.attenuatorLevel && newEnabled == state.attenuatorEnabled)
        return;
    state.attenuatorLevel = level;
    state.attenuatorEnabled = newEnabled;
    emit owner.processingChanged();
}

void handleRASub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 6)
        return;
    const QString raStr = cmd.mid(3);
    if (raStr.length() < 3)
        return;
    bool ok1, ok2;
    const int level = raStr.left(2).toInt(&ok1);
    const int enabled = raStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;
    const bool newEnabled = (enabled == 1);
    if (level == state.attenuatorLevelB && newEnabled == state.attenuatorEnabledB)
        return;
    state.attenuatorLevelB = level;
    state.attenuatorEnabledB = newEnabled;
    emit owner.processingChangedB();
}

void handleGT(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int gt = cmd.mid(2).toInt(&ok);
    if (!ok)
        return;
    int newSpeed;
    if (gt == 0)
        newSpeed = RadioState::AGC_Off;
    else if (gt == 1)
        newSpeed = RadioState::AGC_Slow;
    else if (gt == 2)
        newSpeed = RadioState::AGC_Fast;
    else
        return;
    if (newSpeed == state.agcSpeed)
        return;
    state.agcSpeed = newSpeed;
    emit owner.processingChanged();
}

void handleGTSub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int gt = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    int newSpeed;
    if (gt == 0)
        newSpeed = RadioState::AGC_Off;
    else if (gt == 1)
        newSpeed = RadioState::AGC_Slow;
    else if (gt == 2)
        newSpeed = RadioState::AGC_Fast;
    else
        return;
    if (newSpeed == state.agcSpeedB)
        return;
    state.agcSpeedB = newSpeed;
    emit owner.processingChangedB();
}

void handleNM(ProcessingState &state, RadioState &owner, const QString &cmd) {
    // NM - Manual Notch Main: NMnnnnm or NMm
    if (cmd.length() < 3)
        return;
    const QString data = cmd.mid(2);
    if (data.length() >= 5) {
        bool ok;
        const int pitch = data.left(4).toInt(&ok);
        const bool enabled = (data.at(4) == '1');
        bool changed = false;
        if (ok && pitch >= 150 && pitch <= 5000 && state.manualNotchPitch != pitch) {
            state.manualNotchPitch = pitch;
            changed = true;
        }
        if (state.manualNotchEnabled != enabled) {
            state.manualNotchEnabled = enabled;
            changed = true;
        }
        if (changed)
            emit owner.notchChanged();
    } else if (data.length() >= 1) {
        const bool enabled = (data.at(0) == '1');
        if (state.manualNotchEnabled != enabled) {
            state.manualNotchEnabled = enabled;
            emit owner.notchChanged();
        }
    }
}

void handleNMSub(ProcessingState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    const QString data = cmd.mid(3);
    if (data.length() >= 5) {
        bool ok;
        const int pitch = data.left(4).toInt(&ok);
        const bool enabled = (data.at(4) == '1');
        bool changed = false;
        if (ok && pitch >= 150 && pitch <= 5000 && state.manualNotchPitchB != pitch) {
            state.manualNotchPitchB = pitch;
            changed = true;
        }
        if (state.manualNotchEnabledB != enabled) {
            state.manualNotchEnabledB = enabled;
            changed = true;
        }
        if (changed)
            emit owner.notchBChanged();
    } else if (data.length() >= 1) {
        const bool enabled = (data.at(0) == '1');
        if (state.manualNotchEnabledB != enabled) {
            state.manualNotchEnabledB = enabled;
            emit owner.notchBChanged();
        }
    }
}

void setNoiseBlankerLevel(ProcessingState &state, RadioState &owner, int level) {
    if (state.noiseBlankerLevel != level) {
        state.noiseBlankerLevel = qMin(level, 15);
        emit owner.processingChanged();
    }
}

void setNoiseBlankerLevelB(ProcessingState &state, RadioState &owner, int level) {
    if (state.noiseBlankerLevelB != level) {
        state.noiseBlankerLevelB = qMin(level, 15);
        emit owner.processingChangedB();
    }
}

void setNoiseBlankerFilter(ProcessingState &state, RadioState &owner, int filter) {
    filter = qBound(0, filter, 2);
    if (state.noiseBlankerFilterWidth != filter) {
        state.noiseBlankerFilterWidth = filter;
        emit owner.processingChanged();
    }
}

void setNoiseBlankerFilterB(ProcessingState &state, RadioState &owner, int filter) {
    filter = qBound(0, filter, 2);
    if (state.noiseBlankerFilterWidthB != filter) {
        state.noiseBlankerFilterWidthB = filter;
        emit owner.processingChangedB();
    }
}

void setNoiseReductionLevel(ProcessingState &state, RadioState &owner, int level) {
    if (state.noiseReductionLevel != level) {
        state.noiseReductionLevel = qMin(level, 10);
        emit owner.processingChanged();
    }
}

void setNoiseReductionLevelB(ProcessingState &state, RadioState &owner, int level) {
    if (state.noiseReductionLevelB != level) {
        state.noiseReductionLevelB = qMin(level, 10);
        emit owner.processingChangedB();
    }
}

void setManualNotchPitch(ProcessingState &state, RadioState &owner, int pitch) {
    pitch = qBound(150, pitch, 5000);
    if (state.manualNotchPitch != pitch) {
        state.manualNotchPitch = pitch;
        emit owner.notchChanged();
    }
}

void setManualNotchPitchB(ProcessingState &state, RadioState &owner, int pitch) {
    pitch = qBound(150, pitch, 5000);
    if (state.manualNotchPitchB != pitch) {
        state.manualNotchPitchB = pitch;
        emit owner.notchBChanged();
    }
}

} // namespace ProcessingHandlers
