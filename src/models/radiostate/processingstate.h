#ifndef MODELS_RADIOSTATE_PROCESSINGSTATE_H
#define MODELS_RADIOSTATE_PROCESSINGSTATE_H

#include <QString>

class RadioState;

// Per-VFO signal-processing state: NB / NR / PA / RA / GT (AGC) plus the
// automatic-notch (NA) and manual-notch (NM) subsystems. Mirrors what the
// K4 front panel groups under "RX DSP" + preamp/attenuator controls.
//
// Main-RX and Sub-RX state are kept together on this struct rather than
// split further because (a) the rollup signal processingChanged /
// processingChangedB fires for any change to either's NB/NR/PA/RA/GT
// bundle, so they already share the emit boundary, and (b) the handler
// pairs (handleNB / handleNBSub etc.) are structurally identical so
// keeping them colocated makes the dedup opportunity obvious.
//
// agcSpeed is stored as int to avoid a circular include with radiostate.h
// (RadioState::AGCSpeed is defined inside RadioState). 0=Off 1=Slow 2=Fast
// matches the AGCSpeed enum — see the getter in radiostate.h which casts.
struct ProcessingState {
    // Main RX
    int noiseBlankerLevel = 0;
    bool noiseBlankerEnabled = false;
    int noiseBlankerFilterWidth = 0; // 0=NONE, 1=NARROW, 2=WIDE
    int noiseReductionLevel = 0;
    bool noiseReductionEnabled = false;
    int ssnrLevel = 0; // Spectral-subtraction NR (LMS NR's peer; mutually exclusive on K4)
    bool ssnrEnabled = false;
    bool autoNotchEnabled = false;
    bool manualNotchEnabled = false;
    int manualNotchPitch = 1000; // 150-5000 Hz
    int preamp = 0;
    bool preampEnabled = false;
    int attenuatorLevel = 0;
    bool attenuatorEnabled = false;
    int agcSpeed = 1; // 1=Slow — matches RadioState::AGC_Slow

    // Sub RX
    int noiseBlankerLevelB = 0;
    bool noiseBlankerEnabledB = false;
    int noiseBlankerFilterWidthB = 0;
    int noiseReductionLevelB = 0;
    bool noiseReductionEnabledB = false;
    int ssnrLevelB = 0;
    bool ssnrEnabledB = false;
    bool autoNotchEnabledB = false;
    bool manualNotchEnabledB = false;
    int manualNotchPitchB = 1000;
    int preampB = 0;
    bool preampEnabledB = false;
    int attenuatorLevelB = 0;
    bool attenuatorEnabledB = false;
    int agcSpeedB = 1;

    void reset();
};

namespace ProcessingHandlers {

// CAT handlers.
void handleNB(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNBSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNR(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRS(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRSSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handlePA(ProcessingState &state, RadioState &owner, const QString &cmd);
void handlePASub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleRA(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleRASub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleGT(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleGTSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNM(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNMSub(ProcessingState &state, RadioState &owner, const QString &cmd);

// Optimistic setters.
void setNoiseBlankerLevel(ProcessingState &state, RadioState &owner, int level);
void setNoiseBlankerLevelB(ProcessingState &state, RadioState &owner, int level);
void setNoiseBlankerFilter(ProcessingState &state, RadioState &owner, int filter);
void setNoiseBlankerFilterB(ProcessingState &state, RadioState &owner, int filter);
void setNoiseReductionLevel(ProcessingState &state, RadioState &owner, int level);
void setNoiseReductionLevelB(ProcessingState &state, RadioState &owner, int level);
void setSsnrLevel(ProcessingState &state, RadioState &owner, int level);
void setSsnrLevelB(ProcessingState &state, RadioState &owner, int level);
void setManualNotchPitch(ProcessingState &state, RadioState &owner, int pitch);
void setManualNotchPitchB(ProcessingState &state, RadioState &owner, int pitch);

} // namespace ProcessingHandlers

#endif // MODELS_RADIOSTATE_PROCESSINGSTATE_H
