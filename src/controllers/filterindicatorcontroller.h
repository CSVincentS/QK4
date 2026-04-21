#ifndef FILTERINDICATORCONTROLLER_H
#define FILTERINDICATORCONTROLLER_H

#include <QObject>

class RadioState;
class FilterIndicatorWidget;

// Observes RadioState filter and mode signals and pushes them onto the
// VFO A/B FilterIndicatorWidget instances — filter position (FIL1/2/3),
// bandwidth, IF shift, mode (drives shift-center calculation), and the
// DATA sub-mode (RTTY dual triangles, PSK triangle).
//
// Presentation-only; no CAT dispatch, no RadioState mutations.
class FilterIndicatorController : public QObject {
    Q_OBJECT

public:
    explicit FilterIndicatorController(RadioState *radioState, FilterIndicatorWidget *filterA,
                                       FilterIndicatorWidget *filterB, QObject *parent = nullptr);
    ~FilterIndicatorController() override;

private:
    RadioState *m_radioState;         // injected, not owned
    FilterIndicatorWidget *m_filterA; // injected, not owned
    FilterIndicatorWidget *m_filterB; // injected, not owned
};

#endif // FILTERINDICATORCONTROLLER_H
