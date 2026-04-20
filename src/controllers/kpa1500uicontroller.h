#ifndef KPA1500UICONTROLLER_H
#define KPA1500UICONTROLLER_H

#include <QObject>

class KPA1500Client;
class Kpa1500MiniPanel;
class StatusBarController;

// Owns the KPA1500Client lifetime and keeps the right-side-panel mini
// panel + top-status-bar KPA1500 indicator in sync with the amplifier's
// telemetry and connection state. Also owns the RadioSettings observer
// that reacts to enable/host/port changes and the mini-panel button
// handlers that dispatch KPA control commands (^OS, ^FT, ^AM, ^AN).
//
// MainWindow retains only: (1) a getter to hand the KPA1500Client to
// OptionsDialog for the KPA tab, and (2) two task-level calls —
// connectIfEnabled() on K4 auth success, and disconnectFromHost() on K4
// disconnect — because KPA connectivity is intentionally gated on K4
// connectivity (see setupKpa1500 comment block).
//
// See PATTERNS.md → Controller Pattern.
class KPA1500UiController : public QObject {
    Q_OBJECT

public:
    explicit KPA1500UiController(StatusBarController *statusBar, Kpa1500MiniPanel *miniPanel,
                                 QObject *parent = nullptr);
    ~KPA1500UiController() override;

    // Accessor for OptionsDialog's KPA tab — the dialog configures the
    // client directly (host/port/enable). The controller observes the
    // resulting RadioSettings changes to drive reconnects.
    KPA1500Client *client() const { return m_client; }

    // Connect if RadioSettings has KPA1500 enabled + host configured.
    // Called from MainWindow after K4 auth succeeds — KPA connectivity
    // is gated on K4 connectivity by design.
    void connectIfEnabled();

    // Disconnect from the amplifier. Called from MainWindow when the
    // K4 disconnects so the KPA doesn't keep polling a dead link.
    void disconnectFromHost();

private:
    StatusBarController *m_statusBar;  // injected, not owned
    Kpa1500MiniPanel *m_miniPanel;     // injected, not owned
    KPA1500Client *m_client = nullptr; // owned via Qt parent (this)

    void onConnected();
    void onDisconnected();
    void onError(const QString &error);
    void onEnabledChanged(bool enabled);
    void onSettingsChanged();
    void updateStatus();
};

#endif // KPA1500UICONTROLLER_H
