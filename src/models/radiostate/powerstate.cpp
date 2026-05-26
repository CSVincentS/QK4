#include "powerstate.h"

#include "models/radiostate.h"

void PowerState::reset() {
    powered = -1;
}

namespace PowerHandlers {

void handlePS(PowerState &state, RadioState &owner, const QString &cmd) {
    // Expect "PSx" where x is '0' or '1'. Anything else is malformed — skip.
    if (cmd.length() < 3)
        return;
    const QChar flag = cmd.at(2);
    int next;
    if (flag == '0')
        next = 0;
    else if (flag == '1')
        next = 1;
    else
        return;

    if (next == state.powered)
        return;
    state.powered = next;
    emit owner.powerStateChanged(next == 1);
}

} // namespace PowerHandlers
