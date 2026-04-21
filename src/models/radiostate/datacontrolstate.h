#ifndef MODELS_RADIOSTATE_DATACONTROLSTATE_H
#define MODELS_RADIOSTATE_DATACONTROLSTATE_H

#include <QString>
#include <QtGlobal>

class RadioState;

// Data-mode state: DT (data sub-mode), DR (data rate), VT (VFO tuning
// step), SL (streaming latency). Groups the K4's data-mode-related
// settings that don't fit in the mode/filter subsystem because they're
// conceptually "modifiers" on the mode rather than the mode itself.
//
// Timestamps for the DT / DR optimistic update cooldown are captured
// here too — the K4 doesn't echo DT SET commands, so after we mutate
// the field locally we stamp the time and ignore echoed DT responses
// for 500 ms to avoid race conditions.
//
// Text decode (TD / TB) lives in TextDecodeState (extracted earlier).
struct DataControlState {
    int dataSubMode = -1;      // DT: 0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D (Main)
    int dataSubModeB = -1;     // DT$ (Sub)
    int dataRate = -1;         // DR: 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63) (Main)
    int dataRateB = -1;        // DR$ (Sub)
    int tuningStep = -1;       // VT: 0-5 (1 Hz → 100 kHz)
    int tuningStepB = -1;      // VT$
    int streamingLatency = -1; // SL: 0-7 frame bundling tier

    // Optimistic update cooldown timestamps (milliseconds since epoch).
    qint64 dataSubModeOptimisticTime = 0;
    qint64 dataSubModeBOptimisticTime = 0;
    qint64 dataRateOptimisticTime = 0;
    qint64 dataRateBOptimisticTime = 0;

    void reset();
};

namespace DataControlHandlers {

// CAT handlers.
void handleDT(DataControlState &state, RadioState &owner, const QString &cmd);
void handleDTSub(DataControlState &state, RadioState &owner, const QString &cmd);
void handleDR(DataControlState &state, RadioState &owner, const QString &cmd);
void handleDRSub(DataControlState &state, RadioState &owner, const QString &cmd);
void handleVT(DataControlState &state, RadioState &owner, const QString &cmd);
void handleVTSub(DataControlState &state, RadioState &owner, const QString &cmd);
void handleSL(DataControlState &state, RadioState &owner, const QString &cmd);

// Optimistic setters (DT/DR only — K4 doesn't echo these SET commands).
void setDataSubMode(DataControlState &state, RadioState &owner, int subMode);
void setDataSubModeB(DataControlState &state, RadioState &owner, int subMode);
void setDataRate(DataControlState &state, RadioState &owner, int rate);
void setDataRateB(DataControlState &state, RadioState &owner, int rate);

} // namespace DataControlHandlers

#endif // MODELS_RADIOSTATE_DATACONTROLSTATE_H
