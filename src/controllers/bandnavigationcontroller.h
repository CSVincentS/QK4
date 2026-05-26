#ifndef BANDNAVIGATIONCONTROLLER_H
#define BANDNAVIGATIONCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class PopupManager;

// Routes band-popup selections to the K4's BN / BN$ CAT commands and
// keeps MainWindow's "last-known current band per VFO" state. BSet
// selects which VFO the band change targets; a tap on the already-
// selected band invokes the K4 band stack (BN^ / BN$^) rather than a
// redundant band change.
//
// Owns BN / BN$ CAT echo parsing — subscribes to
// ConnectionController::catResponseReceived in the constructor and
// updates its own band-tracking state. MainWindow does not route BN
// echoes here anymore.
class BandNavigationController : public QObject {
    Q_OBJECT

public:
    explicit BandNavigationController(RadioState *radioState, ConnectionController *connection,
                                      PopupManager *popupManager, QObject *parent = nullptr);
    ~BandNavigationController() override;

    // Last BN echo from the K4 for the indicated VFO. -1 until first echo.
    // Bands 0..10 = HF/6m, 11..22 = XVTR Band 1..12.
    int currentBand(bool forVfoB = false) const { return forVfoB ? m_currentBandNumB : m_currentBandNum; }

signals:
    // Emitted when the K4's current band changes (via BN / BN$ echo). The
    // antenna-label and other UI controllers that need to know "are we on
    // XVTR" subscribe here. forVfoB distinguishes BN$ (true) from BN (false).
    void currentBandChanged(int band, bool forVfoB);

private slots:
    // Connected to PopupManager::bandSelected in constructor.
    void onBandSelected(const QString &bandName);

    // Connected to ConnectionController::catResponseReceived. Parses BN
    // and BN$ echoes; ignores everything else.
    void onCatResponse(const QString &response);

private:
    // Record the current band for a VFO, updating the band popup's
    // selected-band indicator if the selection targets the visible VFO
    // (A when !BSet, B when BSet).
    void setCurrentBand(int bandNum, bool forVfoB);

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    PopupManager *m_popupManager;       // injected, not owned

    int m_currentBandNum = -1;  // last BN echo from the K4
    int m_currentBandNumB = -1; // last BN$ echo from the K4
};

#endif // BANDNAVIGATIONCONTROLLER_H
