#ifndef VFOROWINDICATORCONTROLLER_H
#define VFOROWINDICATORCONTROLLER_H

#include <QObject>

class RadioState;
class SpectrumController;
class VfoRowWidget;
class QLabel;

// Observes RadioState and updates the VFO-row indicator labels —
// SPLIT, VOX, QSK, TEST, ATU, and the message-bank label. Also drives
// the TX-triangle arrows on split changes and calls
// SpectrumController::updateTxMarkers when the split-state flip moves
// the TX VFO.
//
// Scoped intentionally to the "simple" state-label handlers; RIT/XIT
// handling lives elsewhere (RitXitController) because it cross-calls
// frequency display logic. BSet / Sub / Div label styling also stays
// in MainWindow for now — that set changes mode-label color and SUB
// visibility in ways that touch several widgets.
class VfoRowIndicatorController : public QObject {
    Q_OBJECT

public:
    struct Labels {
        QLabel *splitLabel;
        QLabel *txTriangle;
        QLabel *txTriangleB;
        QLabel *voxLabel;
        QLabel *qskLabel;
        QLabel *atuLabel;
        QLabel *msgBankLabel;
    };

    explicit VfoRowIndicatorController(RadioState *radioState, SpectrumController *spectrum, VfoRowWidget *vfoRow,
                                       const Labels &labels, QObject *parent = nullptr);
    ~VfoRowIndicatorController() override;

private slots:
    void onSplitChanged(bool enabled);
    void onVoxChanged();
    void onQskEnabledChanged(bool enabled);
    void onTestModeChanged(bool enabled);
    void onAtuModeChanged(int mode);
    void onMessageBankChanged(int bank);

private:
    RadioState *m_radioState;       // injected, not owned
    SpectrumController *m_spectrum; // injected, not owned — TX-marker refresh on split change
    VfoRowWidget *m_vfoRow;         // injected, not owned — drives test-visible
    Labels m_labels;
};

#endif // VFOROWINDICATORCONTROLLER_H
