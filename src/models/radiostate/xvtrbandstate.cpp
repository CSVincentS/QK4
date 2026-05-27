#include "xvtrbandstate.h"

#include "models/radiostate.h"

namespace XvtrBandHandlers {

namespace {
// Index validity check — K4 sends bands 1..12; convert to 0..11.
bool isValidBand(int oneBased) {
    return oneBased >= 1 && oneBased <= 12;
}
} // namespace

void handleXVN(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int n = cmd.mid(3).toInt(&ok);
    if (!ok || !isValidBand(n))
        return;
    if (state.currentSelect != n) {
        state.currentSelect = n;
        emit owner.xvtrBandSelectChanged(n);
    }
}

void handleXVM(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int m = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.mode != m) {
        band.mode = m;
        emit owner.xvtrBandsChanged();
    }
}

void handleXVR(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int rf = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.rfMhz != rf) {
        band.rfMhz = rf;
        emit owner.xvtrBandsChanged();
    }
}

void handleXVI(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int ifMhz = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.ifMhz != ifMhz) {
        band.ifMhz = ifMhz;
        emit owner.xvtrBandsChanged();
    }
}

void handleXVO(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    // Format: XVO<sign><5-digit Hz>;  e.g., XVO+00000 or XVO-01234.
    QString payload = cmd.mid(3);
    if (payload.isEmpty())
        return;
    bool ok = false;
    const int offset = payload.toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.offsetHz != offset) {
        band.offsetHz = offset;
        emit owner.xvtrBandsChanged();
    }
}

} // namespace XvtrBandHandlers
