#ifndef FEATUREMENUCONTROLLER_H
#define FEATUREMENUCONTROLLER_H

#include <QObject>

#include "ui/featuremenubar.h" // needs the Feature enum for the public API

class RadioState;
class ConnectionController;
class QWidget;

// Owns the FeatureMenuBar popup (the +/-/toggle strip shown above the
// bottom menu bar when the user taps ATTN / NB / NR / NOTCH on the right
// side panel). Drives CAT commands for user edits, applies optimistic
// RadioState updates, and keeps the popup's displayed values in sync
// with RadioState changes from the radio.
//
// MainWindow wires the RightSidePanel button-click signals to
// toggleFeature() and is otherwise out of the loop. See PATTERNS.md →
// Controller Pattern.
class FeatureMenuController : public QObject {
    Q_OBJECT

public:
    explicit FeatureMenuController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                                   QObject *parent = nullptr);
    ~FeatureMenuController() override;

    // Toggle the popup for the given feature. If it's already visible for
    // this feature, hide it; otherwise sync state from RadioState and show
    // it above the given anchor widget (typically m_bottomMenuBar).
    void toggleFeature(FeatureMenuBar::Feature feature, QWidget *anchor);

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    FeatureMenuBar *m_bar;              // owned via Qt parent-ownership (parentWidget)

    // Refresh the bar's displayed value/enabled/filter from current
    // RadioState for the currently-selected feature (no-op if hidden).
    void refreshCurrentFeature();
};

#endif // FEATUREMENUCONTROLLER_H
