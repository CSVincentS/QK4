#ifndef MODELS_RADIOSTATE_XVTRBANDSTATE_H
#define MODELS_RADIOSTATE_XVTRBANDSTATE_H

#include <QString>
#include <QVector>

class RadioState;

// Per-XVTR-band configuration. The K4 supports 12 XVTR (transverter) band
// slots, each independently configured with its RF dial frequency, IF tuned
// on the K4 itself, fine-tune offset, output power, and mode (off/external).
//
// Initial state arrives during RDY as a sequence of XVN<n>; XVM…; XVR…; XVI…;
// XVO…; ME0086.<n>; ME0076…; ME0077…; ... for each of the 12 bands. After
// connect, individual edits (front panel or external CAT) come through as
// XVN<n> followed by whichever fields changed for that band.
struct XvtrBandConfig {
    int mode = 0;              // 0=OFF, 1=External (from XVM<m>)
    int rfMhz = 144;           // dial RF frequency MHz (from XVR<5-digit>)
    int ifMhz = 28;            // IF on the K4 itself (from XVI<2-digit>)
    int offsetHz = 0;          // signed Hz fine-tune (from XVO<signed 5-digit>)
    int powerOutTenthsMw = 10; // 10 = 1.0 mW; matches D10mW MEDF

    bool configured() const { return mode == 1; }
};

struct XvtrBandState {
    // bands[0] = XVTR Band 1, bands[11] = XVTR Band 12 (fixed size).
    QVector<XvtrBandConfig> bands;
    // Currently-selected band index for per-band ME writes (mirrors ME0086 /
    // XVN). 1-based to match K4 conventions; range 1..12.
    int currentSelect = 1;

    XvtrBandState() : bands(12) {}

    void reset() {
        bands = QVector<XvtrBandConfig>(12);
        currentSelect = 1;
    }
};

namespace XvtrBandHandlers {

// XVN<n>; — select XVTR band n (1..12) for subsequent XVM/XVR/XVI/XVO updates.
void handleXVN(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVM<m>; — Mode (0=OFF, 1=External) for currentSelect band.
void handleXVM(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVR<nnnnn>; — RF frequency MHz (e.g., XVR00144 = 144 MHz) for currentSelect band.
void handleXVR(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVI<nn>; — IF frequency MHz (e.g., XVI28 = 28 MHz) for currentSelect band.
void handleXVI(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVO<±nnnnn>; — signed Hz fine-tune offset for currentSelect band.
void handleXVO(XvtrBandState &state, RadioState &owner, const QString &cmd);

} // namespace XvtrBandHandlers

#endif // MODELS_RADIOSTATE_XVTRBANDSTATE_H
