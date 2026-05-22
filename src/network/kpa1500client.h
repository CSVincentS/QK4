#ifndef KPA1500CLIENT_H
#define KPA1500CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QMap>

/**
 * @brief TCP client for a KPA1500 linear amplifier's remote head server. Polls the amp with
 *        `POLL_COMMANDS` on a user-configurable interval, parses the pipelined responses, and
 *        exposes cached state (power, SWR, voltage/current, temperature, band, ATU mode +
 *        inline state, operating state, faults, firmware). Drives Kpa1500MiniPanel + Kpa1500Page.
 */
class KPA1500Client : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Connected };
    Q_ENUM(ConnectionState)

    // Amp operating state (^OS response)
    enum OperatingState { StateUnknown = -1, StateStandby = 0, StateOperate = 1 };
    Q_ENUM(OperatingState)

    // Fault status (^FS response)
    enum FaultStatus { FaultNone = 0, FaultActive = 1, FaultHistory = 2 };
    Q_ENUM(FaultStatus)

    explicit KPA1500Client(QObject *parent = nullptr);
    ~KPA1500Client();

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    ConnectionState connectionState() const;

    void sendCommand(const QString &command);
    void startPolling(int intervalMs);
    void stopPolling();

    // State getters
    QString bandName() const { return m_bandName; }
    double forwardPower() const { return m_forwardPower; }
    double reflectedPower() const { return m_reflectedPower; }
    double drivePower() const { return m_drivePower; }
    double swr() const { return m_swr; }
    double paTemperature() const { return m_paTemperature; }
    OperatingState operatingState() const { return m_operatingState; }
    FaultStatus faultStatus() const { return m_faultStatus; }
    QString faultCode() const { return m_faultCode; }
    bool atuPresent() const { return m_atuPresent; }
    bool atuModeInline() const { return m_atuModeInline; } // ^AM: ATU mode (enabled/disabled)
    bool atuInline() const { return m_atuInline; }         // ^AI: ATU relay state (in-circuit/bypassed)
    int antenna() const { return m_antenna; }
    QString serialNumber() const { return m_serialNumber; }
    QString firmwareVersion() const { return m_firmwareVersion; }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

    // Data update signals
    void powerChanged(double forward, double reflected, double drive);
    void swrChanged(double swr);
    void paTemperatureChanged(double tempC);
    void operatingStateChanged(OperatingState state);
    void faultStatusChanged(FaultStatus status, const QString &faultCode);
    void atuModeChanged(bool inline_);   // ^AM: ATU mode toggled
    void atuInlineChanged(bool inline_); // ^AI: ATU relay state changed
    void antennaChanged(int antenna);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onPollTimer();

private:
    void setState(ConnectionState state);
    void parseResponse(const QString &response);
    void parseSingleResponse(const QString &response);

    QTcpSocket *m_socket;
    QTimer *m_pollTimer;
    QString m_receiveBuffer;

    QString m_host;
    quint16 m_port;
    ConnectionState m_state;

    // Poll command string
    static const QString POLL_COMMANDS;

    // Cached state values
    QString m_bandName;
    double m_forwardPower = 0.0;
    double m_reflectedPower = 0.0;
    double m_drivePower = 0.0;
    double m_swr = 1.0;
    double m_paTemperature = 0.0;
    OperatingState m_operatingState = StateUnknown;
    FaultStatus m_faultStatus = FaultNone;
    QString m_faultCode;
    bool m_atuPresent = false;
    bool m_atuModeInline = false; // ^AM: ATU mode (I=inline/enabled, B=bypassed/disabled)
    bool m_atuInline = false;     // ^AI: ATU relay state (1=relays inline, 0=relays bypassed)
    int m_antenna = 1;
    QString m_serialNumber;
    QString m_firmwareVersion;
};

#endif // KPA1500CLIENT_H
