#ifndef MODELS_RADIOSTATE_RXTXMETERSTATE_H
#define MODELS_RADIOSTATE_RXTXMETERSTATE_H

#include <QMap>
#include <QString>

class RadioState;

// Operational / meter / radio-identity state. This subsystem owns all
// non-VFO, non-audio, non-spectrum "what the radio is reporting right
// now" telemetry — the RX/TX transition, the S-meters, the TX meter
// cluster (ALC/CMP/FWD/SWR), power-supply readings, radio identity
// (ID/OM/RV.), message bank, and a handful of miscellaneous mode
// toggles that ride along on the same signal path (SB, DV, TS, BS).
//
// Covers: TX, RX, SM, SM$, PO, TM, ID, OM, RV., ER, MN, SIFP, SB, DV,
// TS, BS.
struct RxTxMeterState {
    // S-meters (SM / SM$) in S-units (0..9.9 before decimal, 9.x for dB over).
    double sMeter = 0.0;
    double sMeterB = 0.0;

    // TX meter cluster (TM).
    double swrMeter = 1.0;
    int alcMeter = 0;
    int compressionDb = 0;
    double forwardPower = 0.0;

    // Power supply (SIFP).
    double supplyVoltage = 0.0;
    double supplyCurrent = 0.0;

    // PA drain current (SIRF PM divided by 768). Empirically matches the K4 front-panel
    // "Id" meter within ~1 A across the full power range. PM is the peak-held aggregate
    // the K4's own display shows; LM (in the same SIRF frame) is just one stage's drain
    // and reads ~5–7 A low.
    double paDrainCurrent = 0.0;

    // PA efficiency calibration anchor. Computed on every valid SIRF frame as
    //     η = fwdPower / (paDrainCurrent × supplyVoltage)
    // and then used in handleTM to derive a live Id reading from forward power at TM
    // frame rate (~10 Hz). This combines SIRF's accuracy (anchored to the K4 panel)
    // with TM's smoothness — without the 0.34 hardcoded-η error that historically
    // under-read by 5–7 A at low power. Reset to 0 (= "no calibration yet, fall back
    // to fixed η") on reset().
    double calibratedPaEfficiency = 0.0;

    // TX/RX transition.
    bool isTransmitting = false;

    // Control toggles carried alongside the TX/RX feed.
    bool subReceiverEnabled = false;
    bool diversityEnabled = false;
    bool testMode = false;
    bool bSetEnabled = false;

    // Message bank (MN).
    int messageBank = -1;

    // Radio identity (ID, OM, RV.).
    QString radioID;
    QString radioModel;
    QString optionModules;
    QMap<QString, QString> firmwareVersions;

    void reset();
};

namespace RxTxMeterHandlers {

void handleSM(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleSMSub(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handlePO(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleTM(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleTX(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleRX(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleID(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleOM(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleRV(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleER(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleMN(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleSIFP(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleSIRF(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleSB(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleDV(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleTS(RxTxMeterState &state, RadioState &owner, const QString &cmd);
void handleBS(RxTxMeterState &state, RadioState &owner, const QString &cmd);

// BS is also set optimistically from the UI (SW44 toggle), so a setter
// is exposed as well. The other toggles flow in only from the radio.
void setBSetEnabled(RxTxMeterState &state, RadioState &owner, bool enabled);

} // namespace RxTxMeterHandlers

#endif // MODELS_RADIOSTATE_RXTXMETERSTATE_H
