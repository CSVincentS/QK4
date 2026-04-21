#include "datacontrolstate.h"

#include "models/radiostate.h"

#include <QDateTime>
#include <QtGlobal>

void DataControlState::reset() {
    dataSubMode = -1;
    dataSubModeB = -1;
    dataRate = -1;
    dataRateB = -1;
    tuningStep = -1;
    tuningStepB = -1;
    streamingLatency = -1;
    dataSubModeOptimisticTime = 0;
    dataSubModeBOptimisticTime = 0;
    dataRateOptimisticTime = 0;
    dataRateBOptimisticTime = 0;
}

namespace DataControlHandlers {

void handleDT(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int subMode = cmd.mid(2).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 500 ms cooldown — ignore echoed DT while recent optimistic SET still in flight.
    if (now - state.dataSubModeOptimisticTime < 500)
        return;
    if (subMode != state.dataSubMode) {
        state.dataSubMode = subMode;
        emit owner.dataSubModeChanged(state.dataSubMode);
    }
}

void handleDTSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int subMode = cmd.mid(3).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - state.dataSubModeBOptimisticTime < 500)
        return;
    if (subMode != state.dataSubModeB) {
        state.dataSubModeB = subMode;
        emit owner.dataSubModeBChanged(state.dataSubModeB);
    }
}

void handleDR(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int rate = cmd.mid(2).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - state.dataRateOptimisticTime < 500)
        return;
    if (rate != state.dataRate) {
        state.dataRate = rate;
        emit owner.dataRateChanged(state.dataRate);
    }
}

void handleDRSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int rate = cmd.mid(3).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - state.dataRateBOptimisticTime < 500)
        return;
    if (rate != state.dataRateB) {
        state.dataRateB = rate;
        emit owner.dataRateBChanged(state.dataRateB);
    }
}

void handleVT(DataControlState &state, RadioState &owner, const QString &cmd) {
    // VT - tuning step. Takes the first digit from the payload.
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int step = cmd.mid(2).left(1).toInt(&ok);
    if (!ok)
        return;
    const int ns = qBound(0, step, 5);
    if (ns != state.tuningStep) {
        state.tuningStep = ns;
        emit owner.tuningStepChanged(state.tuningStep);
    }
}

void handleVTSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int step = cmd.mid(3).left(1).toInt(&ok);
    if (!ok)
        return;
    const int ns = qBound(0, step, 5);
    if (ns != state.tuningStepB) {
        state.tuningStepB = ns;
        emit owner.tuningStepBChanged(state.tuningStepB);
    }
}

void handleSL(DataControlState &state, RadioState &owner, const QString &cmd) {
    // SL - streaming latency tier 0-7 (see k4-streaming-latency.md).
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int tier = cmd.mid(2).toInt(&ok);
    if (!ok || tier < 0 || tier > 7)
        return;
    if (tier != state.streamingLatency) {
        state.streamingLatency = tier;
        emit owner.streamingLatencyChanged(state.streamingLatency);
    }
}

void setDataSubMode(DataControlState &state, RadioState &owner, int subMode) {
    subMode = qBound(0, subMode, 3);
    if (state.dataSubMode != subMode) {
        state.dataSubMode = subMode;
        state.dataSubModeOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit owner.dataSubModeChanged(subMode);
    }
}

void setDataSubModeB(DataControlState &state, RadioState &owner, int subMode) {
    subMode = qBound(0, subMode, 3);
    if (state.dataSubModeB != subMode) {
        state.dataSubModeB = subMode;
        state.dataSubModeBOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit owner.dataSubModeBChanged(subMode);
    }
}

void setDataRate(DataControlState &state, RadioState &owner, int rate) {
    rate = qBound(0, rate, 1);
    if (state.dataRate != rate) {
        state.dataRate = rate;
        state.dataRateOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit owner.dataRateChanged(rate);
    }
}

void setDataRateB(DataControlState &state, RadioState &owner, int rate) {
    rate = qBound(0, rate, 1);
    if (state.dataRateB != rate) {
        state.dataRateB = rate;
        state.dataRateBOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit owner.dataRateBChanged(rate);
    }
}

} // namespace DataControlHandlers
