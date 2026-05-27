#ifndef POWERSTATUSBUTTON_H
#define POWERSTATUSBUTTON_H

#include "ui/styling/k4glyphs.h"

#include <QToolButton>

// Status-bar power icon. Doubles as a connection / power-state indicator and
// a click target for the K4 remote power-off flow.
//
// States:
//   - Unknown — grey, non-clickable. Used between connect and the first PS
//     query response.
//   - On      — green, clickable. Clicking emits the inherited clicked()
//     signal; the StatusBarController handles confirmation + PS0 send.
//   - Off     — red, non-clickable. K4 is powered off / not connected. The
//     widget is intentionally display-only in this state (per design: QK4
//     cannot power the K4 back on, so a clickable red icon would mislead).
class PowerStatusButton : public QToolButton {
    Q_OBJECT

public:
    enum class State { Unknown, On, Off };

    explicit PowerStatusButton(QWidget *parent = nullptr);

    void setState(State state);
    State state() const { return m_state; }

    // The inherited QAbstractButton::clicked() signal is used as-is. It only
    // fires in State::On because Off / Unknown disable the button.

private:
    void applyAppearance();

    State m_state = State::Unknown;
    K4Glyphs::Glyph m_glyph;
};

#endif // POWERSTATUSBUTTON_H
