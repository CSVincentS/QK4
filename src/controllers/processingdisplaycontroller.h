#ifndef PROCESSINGDISPLAYCONTROLLER_H
#define PROCESSINGDISPLAYCONTROLLER_H

#include <QObject>

class RadioState;
class VFOWidget;

// Mirrors the K4's audio-processing state (AGC speed, preamp level,
// attenuator level, Noise Blanker, Noise Reduction) onto the per-VFO
// widget indicators. Observes RadioState's processingChanged and
// processingChangedB rollup signals and refreshes all five fields as
// a batch — the K4 emits these as grouped "processing" updates so a
// single rollup handler is the natural fit.
//
// This is a presentation-only controller; it sends no CAT and owns no
// widgets. It exists to lift the ~60-LOC onProcessingChanged pair out
// of MainWindow and keep per-VFO display mirroring in one place.
//
// See PATTERNS.md → Controller Pattern.
class ProcessingDisplayController : public QObject {
    Q_OBJECT

public:
    explicit ProcessingDisplayController(RadioState *radioState, VFOWidget *vfoA, VFOWidget *vfoB,
                                         QObject *parent = nullptr);
    ~ProcessingDisplayController() override;

private slots:
    void refreshVfoA();
    void refreshVfoB();

private:
    RadioState *m_radioState; // injected, not owned
    VFOWidget *m_vfoA;        // injected, not owned
    VFOWidget *m_vfoB;        // injected, not owned
};

#endif // PROCESSINGDISPLAYCONTROLLER_H
