#pragma once

#include "models/radiostate.h"

#include <QHash>
#include <QObject>
#include <QTcpSocket>

class CatPushBroadcaster : public QObject {
    Q_OBJECT

public:
    explicit CatPushBroadcaster(RadioState *state, QObject *parent = nullptr);
    ~CatPushBroadcaster() override;

    void addClient(QTcpSocket *client);
    void removeClient(QTcpSocket *client);
    void setClientAiMode(QTcpSocket *client, int mode);
    int clientAiMode(QTcpSocket *client) const;

    int subscriberCount() const;

private slots:
    void onFrequencyChanged(quint64 freq);
    void onFrequencyBChanged(quint64 freq);
    void onModeChanged(RadioState::Mode m);
    void onModeBChanged(RadioState::Mode m);
    void onTransmitStateChanged(bool tx);
    void onSplitChanged(bool on);
    void onRitXitChanged(bool rit, bool xit, int offset);
    void onRfPowerChanged(double watts, bool qrp);
    void onFilterBandwidthChanged(int bw);
    void onKeyerSpeedChanged(int wpm);
    void onVoxChanged(bool on);
    void onDiversityChanged(bool on);
    void onDataSubModeChanged(int subMode);
    void onProcessingChanged();

private:
    void broadcast(const QByteArray &frame);

    RadioState *m_radioState;
    QHash<QTcpSocket *, int> m_clientAiModes;

    bool m_lastNB = false;
    bool m_lastNR = false;
    int m_lastAGC = -1;
    bool m_processingCacheInitialized = false;
};
