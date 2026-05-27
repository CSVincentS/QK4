#ifndef MODELS_RADIOSTATE_POWERSTATE_H
#define MODELS_RADIOSTATE_POWERSTATE_H

#include <QString>
#include <optional>

class RadioState;

// K4 remote power state. Tracks the K4's PS query/echo: PS1 = on, PS0 = off.
// PS0 is also the SET command QK4 uses to remotely power the K4 off (the K4
// closes the TCP socket shortly after acknowledging).
//
// Plain struct — follows Pattern C (see PATTERNS.md → Subsystem State).
struct PowerState {
    // Tri-state: -1 = unknown (not seen yet / post-reset), 0 = off, 1 = on.
    // Tri-state lets the UI distinguish "haven't asked yet" from "asked and
    // got PS0" so the indicator can render neutral instead of red until the
    // initial PS query response arrives.
    int powered = -1;

    void reset();

    // Convenience accessor that lifts the tri-state into std::optional<bool>.
    std::optional<bool> isOn() const {
        if (powered < 0)
            return std::nullopt;
        return powered == 1;
    }
};

namespace PowerHandlers {

// Parses PS0 / PS1 and emits powerStateChanged(bool) via the façade when the
// value actually changes.
void handlePS(PowerState &state, RadioState &owner, const QString &cmd);

} // namespace PowerHandlers

#endif // MODELS_RADIOSTATE_POWERSTATE_H
