#include "filterindicatorcontroller.h"

#include "models/radiostate.h"
#include "ui/filterindicatorwidget.h"

FilterIndicatorController::FilterIndicatorController(RadioState *radioState, FilterIndicatorWidget *filterA,
                                                     FilterIndicatorWidget *filterB, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_filterA(filterA), m_filterB(filterB) {
    // Filter position (FIL1/FIL2/FIL3).
    connect(m_radioState, &RadioState::filterPositionChanged, this,
            [this](int pos) { m_filterA->setFilterPosition(pos); });
    connect(m_radioState, &RadioState::filterPositionBChanged, this,
            [this](int pos) { m_filterB->setFilterPosition(pos); });

    // Bandwidth and shift drive the shape + position of the indicator.
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, [this](int bw) { m_filterA->setBandwidth(bw); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this, [this](int bw) { m_filterB->setBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_filterA->setShift(shift); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_filterB->setShift(shift); });

    // Mode affects shift-center calculation (SSB vs CW).
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_filterA->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_filterB->setMode(RadioState::modeToString(mode)); });

    // DATA sub-mode triggers RTTY/PSK tone-marker overlays on the shape.
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_filterA->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_filterB->setDataSubMode(subMode); });
}

FilterIndicatorController::~FilterIndicatorController() {
    disconnect(this);
}
