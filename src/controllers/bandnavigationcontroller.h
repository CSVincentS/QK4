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
// BN / BN$ CAT echoes are parsed in MainWindow::onCatResponse and
// forwarded here via setCurrentBand().
class BandNavigationController : public QObject {
    Q_OBJECT

public:
    explicit BandNavigationController(RadioState *radioState, ConnectionController *connection,
                                      PopupManager *popupManager, QObject *parent = nullptr);
    ~BandNavigationController() override;

    // Record the current band for a VFO, updating the band popup's
    // selected-band indicator if the selection targets the visible VFO
    // (A when !BSet, B when BSet).
    void setCurrentBand(int bandNum, bool forVfoB);

private slots:
    // Connected to PopupManager::bandSelected in constructor.
    void onBandSelected(const QString &bandName);

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    PopupManager *m_popupManager;       // injected, not owned

    int m_currentBandNum = -1;  // last BN echo from the K4
    int m_currentBandNumB = -1; // last BN$ echo from the K4
};

#endif // BANDNAVIGATIONCONTROLLER_H
