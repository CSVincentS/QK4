#include "popupmanager.h"

#include "connectioncontroller.h"
#include "dsp/panadapter_rhi.h"
#include "models/radiostate.h"
#include "spectrumcontroller.h"
#include "ui/bandpopupwidget.h"
#include "ui/bottommenubar.h"
#include "ui/displaypopupwidget.h"
#include "ui/fnpopupwidget.h"
#include "ui/macrodialog.h"
#include "ui/vfowidget.h"
#include "utils/radioutils.h"

#include <QPoint>
#include <QString>
#include <QWidget>

PopupManager::PopupManager(RadioState *radioState, ConnectionController *connection, SpectrumController *spectrum,
                           VFOWidget *vfoA, VFOWidget *vfoB, QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_spectrum(spectrum), m_vfoA(vfoA),
      m_vfoB(vfoB), m_parentWidget(parentWidget), m_bandPopup(new BandPopupWidget(parentWidget)),
      m_displayPopup(new DisplayPopupWidget(parentWidget)), m_fnPopup(new FnPopupWidget(parentWidget)),
      m_macroDialog(new MacroDialog(parentWidget)) {

    m_macroDialog->hide();

    // --- Band popup ---
    connect(m_bandPopup, &BandPopupWidget::bandSelected, this, &PopupManager::bandSelected);
    connect(m_bandPopup, &BandPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setBandActive(false);
    });

    // --- Display popup ---
    wireDisplayPopup();

    // --- RadioState → DisplayPopup state sync ---
    // These used to live in MainWindow::setupRadioStateWiring; they
    // logically belong with the popup that consumes them.
    connect(m_radioState, &RadioState::dualPanModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeLcd);
    connect(m_radioState, &RadioState::dualPanModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeExt);
    connect(m_radioState, &RadioState::displayModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeLcd);
    connect(m_radioState, &RadioState::displayModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeExt);
    connect(m_radioState, &RadioState::waterfallColorChanged, m_displayPopup, &DisplayPopupWidget::setWaterfallColor);
    connect(m_radioState, &RadioState::averagingChanged, m_displayPopup, &DisplayPopupWidget::setAveraging);
    connect(m_radioState, &RadioState::peakModeChanged, m_displayPopup, &DisplayPopupWidget::setPeakMode);
    connect(m_radioState, &RadioState::fixedTuneChanged, m_displayPopup, &DisplayPopupWidget::setFixedTuneMode);
    connect(m_radioState, &RadioState::freezeChanged, m_displayPopup, &DisplayPopupWidget::setFreeze);
    connect(m_radioState, &RadioState::vfoACursorChanged, m_displayPopup, &DisplayPopupWidget::setVfoACursor);
    connect(m_radioState, &RadioState::vfoBCursorChanged, m_displayPopup, &DisplayPopupWidget::setVfoBCursor);
    connect(m_radioState, &RadioState::autoRefLevelChanged, m_displayPopup, &DisplayPopupWidget::setAutoRefLevel);
    connect(m_radioState, &RadioState::scaleChanged, m_displayPopup, &DisplayPopupWidget::setScale);
    connect(m_radioState, &RadioState::ddcNbModeChanged, m_displayPopup, &DisplayPopupWidget::setDdcNbMode);
    connect(m_radioState, &RadioState::ddcNbLevelChanged, m_displayPopup, &DisplayPopupWidget::setDdcNbLevel);
    connect(m_radioState, &RadioState::waterfallHeightChanged, m_displayPopup, &DisplayPopupWidget::setWaterfallHeight);
    connect(m_radioState, &RadioState::waterfallHeightExtChanged, m_displayPopup,
            &DisplayPopupWidget::setWaterfallHeightExt);
    connect(m_radioState, &RadioState::spanChanged, this,
            [this](int spanHz) { m_displayPopup->setSpanValueA(spanHz / 1000.0); });
    connect(m_radioState, &RadioState::spanBChanged, this,
            [this](int spanHz) { m_displayPopup->setSpanValueB(spanHz / 1000.0); });
    connect(m_radioState, &RadioState::refLevelChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueA);
    connect(m_radioState, &RadioState::refLevelBChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueB);

    // --- Fn popup ---
    connect(m_fnPopup, &FnPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setFnActive(false);
    });
    connect(m_fnPopup, &FnPopupWidget::functionTriggered, this, &PopupManager::macroFunctionTriggered);
}

PopupManager::~PopupManager() {
    disconnect(this);
}

void PopupManager::setBottomMenuBar(BottomMenuBar *bottomMenuBar) {
    m_bottomMenuBar = bottomMenuBar;
}

void PopupManager::toggleBand() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_bandPopup->isVisible();
    closeOwnedPopups();
    if (!wasVisible) {
        m_bandPopup->showAboveButton(m_bottomMenuBar->bandButton());
        m_bottomMenuBar->setBandActive(true);
    }
}

void PopupManager::toggleDisplay() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_displayPopup->isVisible();
    closeOwnedPopups();
    if (!wasVisible) {
        m_displayPopup->showAboveButton(m_bottomMenuBar->displayButton());
        m_bottomMenuBar->setDisplayActive(true);
    }
}

void PopupManager::toggleFn() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_fnPopup->isVisible();
    closeOwnedPopups();
    if (!wasVisible) {
        m_fnPopup->showAboveButton(m_bottomMenuBar->fnButton());
        m_bottomMenuBar->setFnActive(true);
    }
}

void PopupManager::openMacroDialog() {
    closeOwnedPopups();

    // Size the macro dialog to fill the spectrum container (same overlay
    // positioning the menu overlay uses).
    if (m_spectrum->spectrumContainer()) {
        const QPoint pos = m_spectrum->spectrumContainer()->mapTo(m_parentWidget, QPoint(0, 0));
        m_macroDialog->setGeometry(pos.x(), pos.y(), m_spectrum->spectrumContainer()->width(),
                                   m_spectrum->spectrumContainer()->height());
    }
    m_macroDialog->show();
    m_macroDialog->raise();
    m_macroDialog->setFocus();
}

void PopupManager::closeOwnedPopups() {
    if (m_bandPopup->isVisible()) {
        m_bandPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setBandActive(false);
    }
    if (m_displayPopup->isVisible()) {
        m_displayPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setDisplayActive(false);
    }
    if (m_fnPopup->isVisible()) {
        m_fnPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setFnActive(false);
    }
}

void PopupManager::hideMacroDialog() {
    m_macroDialog->hide();
}

int PopupManager::bandNumberForName(const QString &bandName) const {
    return m_bandPopup->getBandNumber(bandName);
}

void PopupManager::setSelectedBandByNumber(int bandNum) {
    m_bandPopup->setSelectedBandByNumber(bandNum);
}

void PopupManager::wireDisplayPopup() {
    connect(m_displayPopup, &DisplayPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setDisplayActive(false);
    });

    // DisplayPopup pan mode changed → panadapter display mode.
    // (K4 doesn't echo #DPM commands, so DisplayPopup notifies us directly.)
    connect(m_displayPopup, &DisplayPopupWidget::dualPanModeChanged, this, [this](int mode) {
        switch (mode) {
        case 0:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::MainOnly);
            break;
        case 1:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::SubOnly);
            break;
        case 2:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::Dual);
            break;
        }
    });

    // DisplayPopup-generated raw CAT commands → connection controller.
    connect(m_displayPopup, &DisplayPopupWidget::catCommandRequested, this,
            [this](const QString &cmd) { m_connection->sendCAT(cmd); });

    // Averaging control +/- → local only (our smoothing differs from K4's).
    connect(m_displayPopup, &DisplayPopupWidget::averagingIncrementRequested, this, [this]() {
        const int next = qMin(m_radioState->averaging() + 1, 20);
        m_radioState->setAveraging(next);
    });
    connect(m_displayPopup, &DisplayPopupWidget::averagingDecrementRequested, this, [this]() {
        const int next = qMax(m_radioState->averaging() - 1, 1);
        m_radioState->setAveraging(next);
    });

    // DDC NB level control +/- → CAT.
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelIncrementRequested, this, [this]() {
        const int next = qMin(m_radioState->ddcNbLevel() + 1, 14);
        m_connection->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelDecrementRequested, this, [this]() {
        const int next = qMax(m_radioState->ddcNbLevel() - 1, 0);
        m_connection->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });

    // Waterfall height +/- (LCD or EXT depending on popup selection).
    auto adjustWaterfallHeight = [this](int delta) {
        const bool isExt = m_displayPopup->isExtEnabled() && !m_displayPopup->isLcdEnabled();
        const int current = isExt ? m_radioState->waterfallHeightExt() : m_radioState->waterfallHeight();
        const int next = qBound(10, current + delta, 90);
        const QString cmd =
            isExt ? QString("#HWFH%1;").arg(next, 2, 10, QChar('0')) : QString("#WFH%1;").arg(next, 2, 10, QChar('0'));
        m_connection->sendCAT(cmd);
        // Optimistic updates — K4 may not echo this command.
        if (!isExt) {
            m_radioState->setWaterfallHeight(next);
            m_spectrum->panadapterA()->setWaterfallHeight(next);
            m_spectrum->panadapterB()->setWaterfallHeight(next);
            m_displayPopup->setWaterfallHeight(next);
            m_vfoA->setMiniPanWaterfallHeight(next);
            m_vfoB->setMiniPanWaterfallHeight(next);
        } else {
            m_radioState->setWaterfallHeightExt(next);
            m_displayPopup->setWaterfallHeightExt(next);
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightIncrementRequested, this,
            [adjustWaterfallHeight]() { adjustWaterfallHeight(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightDecrementRequested, this,
            [adjustWaterfallHeight]() { adjustWaterfallHeight(-1); });

    // Span +/- (per-VFO selection).
    auto adjustSpan = [this](bool increment) {
        const bool vfoA = m_displayPopup->isVfoAEnabled();
        const bool vfoB = m_displayPopup->isVfoBEnabled();
        const int currentSpan = (vfoB && !vfoA) ? m_radioState->spanHzB() : m_radioState->spanHz();
        const int newSpan =
            increment ? RadioUtils::getNextSpanUp(currentSpan) : RadioUtils::getNextSpanDown(currentSpan);
        if (newSpan == currentSpan)
            return;
        if (vfoA) {
            m_radioState->setSpanHz(newSpan);
            m_connection->sendCAT(QString("#SPN%1;").arg(newSpan));
        }
        if (vfoB) {
            m_radioState->setSpanHzB(newSpan);
            m_connection->sendCAT(QString("#SPN$%1;").arg(newSpan));
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::spanIncrementRequested, this, [adjustSpan]() { adjustSpan(true); });
    connect(m_displayPopup, &DisplayPopupWidget::spanDecrementRequested, this, [adjustSpan]() { adjustSpan(false); });

    // Scale +/- (global — no A/B variants).
    auto adjustScale = [this](int delta) {
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75; // Default if not yet received
        const int newScale = qBound(10, currentScale + delta, 150);
        if (newScale == currentScale)
            return;
        m_connection->sendCAT(QString("#SCL%1;").arg(newScale));
        // Scale is global and may not echo back; update optimistically.
        m_radioState->setScale(newScale);
    };
    connect(m_displayPopup, &DisplayPopupWidget::scaleIncrementRequested, this, [adjustScale]() { adjustScale(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::scaleDecrementRequested, this, [adjustScale]() { adjustScale(-1); });

    // Ref level +/- (per-VFO, 1 dB step, range -200..60).
    auto adjustRefLevel = [this](int delta) {
        const bool vfoA = m_displayPopup->isVfoAEnabled();
        const bool vfoB = m_displayPopup->isVfoBEnabled();
        if (vfoA) {
            const int newLevel = qBound(-200, m_radioState->refLevel() + delta, 60);
            if (newLevel != m_radioState->refLevel()) {
                m_radioState->setRefLevel(newLevel);
                m_connection->sendCAT(QString("#REF%1;").arg(newLevel));
            }
        }
        if (vfoB) {
            const int newLevel = qBound(-200, m_radioState->refLevelB() + delta, 60);
            if (newLevel != m_radioState->refLevelB()) {
                m_radioState->setRefLevelB(newLevel);
                m_connection->sendCAT(QString("#REF$%1;").arg(newLevel));
            }
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::refLevelIncrementRequested, this,
            [adjustRefLevel]() { adjustRefLevel(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::refLevelDecrementRequested, this,
            [adjustRefLevel]() { adjustRefLevel(-1); });
}
