#ifndef MENUCONTROLLER_H
#define MENUCONTROLLER_H

#include <QObject>

class ConnectionController;
class SpectrumController;
class MenuModel;
class MenuOverlayWidget;
class QWidget;

// Owns the K4 MEDF menu system: the MenuModel (menu item cache), the
// MenuOverlayWidget (full-screen menu browser shown over the spectrum),
// and the discovery-state sentinels for a few special menu items whose
// IDs the K4 reports dynamically (Mouse L/R QSY, FSK Mark-Tone, Spectrum
// Amplitude Units).
//
// MainWindow retains: onBandSelected (the BandPopup is separate), the
// bottom menu bar wiring (menuClicked → showMenuOverlay), and the
// onCatResponse MEDF routing (forwards cmd to menuModel()->parseMEDF/parseME).
//
// See PATTERNS.md → Controller Pattern.
class MenuController : public QObject {
    Q_OBJECT

public:
    explicit MenuController(ConnectionController *connection, SpectrumController *spectrum, QWidget *parentWidget,
                            QObject *parent = nullptr);
    ~MenuController() override;

    // For MainWindow::onCatResponse — routes MEDF/ME CAT lines into the
    // menu model. Exposing menuModel() is the minimum-surface way to keep
    // that routing working. (An alternative is ingestMedf()/ingestMe()
    // methods on MenuController, but they'd be 1-line forwarders.)
    MenuModel *menuModel() const;

    // Task-level overlay API. Positions the overlay above the spectrum
    // container (resolved internally from the injected SpectrumController).
    void toggleOverlay();
    void closeOverlay();
    bool isOverlayVisible() const;

    // MainWindow::onRadioReady adds a synthetic Display FPS menu item
    // (value updates from radio echoes). Exposed as a task-level method.
    void addSyntheticDisplayFpsItem(int initialFps);

    // RadioState::displayFpsChanged comes from a MEDF#-prefix handler and
    // needs to propagate to the synthetic menu item so the overlay shows
    // the live value. MainWindow keeps the RadioState signal connection;
    // forwards to this setter.
    void setDisplayFps(int fps);

    // Menu overlay geometry (relative to parent) is derived from the
    // spectrum container. This is called automatically by toggleOverlay()
    // but exposed in case MainWindow needs to reposition on layout change.
    void repositionOverlay();

signals:
    // Overlay was hidden (by escape, click-outside, or a button dismiss).
    // MainWindow connects this to BottomMenuBar::setMenuActive(false).
    void overlayClosed();

private:
    ConnectionController *m_connection; // injected, not owned
    SpectrumController *m_spectrum;     // injected, not owned
    QWidget *m_parentWidget;            // used for overlay geometry mapping

    MenuModel *m_menuModel;           // owned: plain QObject, deleted via this
    MenuOverlayWidget *m_menuOverlay; // owned via Qt parent-ownership (parentWidget)

    // Sentinel-initialized so we can detect "menu ID not yet discovered".
    // When the K4 advertises these items in MEDF, onMenuItemAdded stores
    // the ID so onMenuModelValueChanged can recognize later edits.
    int m_mouseQsyMode = 0;
    int m_mouseQsyMenuId = -999;
    int m_fskMarkToneMenuId = -999;

    void onMenuValueChangeRequested(int menuId, const QString &action);
    void onMenuModelValueChanged(int menuId, int newValue);
    void onMenuItemAdded(int menuId);
};

#endif // MENUCONTROLLER_H
