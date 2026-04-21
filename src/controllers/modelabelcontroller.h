#ifndef MODELABELCONTROLLER_H
#define MODELABELCONTROLLER_H

#include <QObject>

class RadioState;
class QLabel;

// Owns the VFO A / VFO B mode label text. Observes RadioState's mode,
// data-submode, and ESSB signals and re-renders both labels as a pair
// (the ESSB "+" suffix only applies to USB/LSB, but it applies equally
// to whichever VFO is in that mode).
//
// Label WIDGETS are still created by MainWindow (they live in the VFO
// row) — the controller just observes state and calls setText. Color /
// styling changes driven by BSET and SUB RX remain in MainWindow, since
// those overlap with other indicator styling that hasn't been extracted.
class ModeLabelController : public QObject {
    Q_OBJECT

public:
    explicit ModeLabelController(RadioState *radioState, QLabel *modeALabel, QLabel *modeBLabel,
                                 QObject *parent = nullptr);
    ~ModeLabelController() override;

    // Recompute + set both labels from current RadioState. Public so
    // other controllers / MainWindow can trigger a refresh (e.g., after
    // an optimistic ESSB toggle before the radio's ES echo arrives).
    void refresh();

private:
    RadioState *m_radioState; // injected, not owned
    QLabel *m_modeALabel;     // injected, not owned
    QLabel *m_modeBLabel;     // injected, not owned
};

#endif // MODELABELCONTROLLER_H
