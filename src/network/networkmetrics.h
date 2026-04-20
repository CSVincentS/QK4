#ifndef NETWORKMETRICS_H
#define NETWORKMETRICS_H

#include <QObject>
#include <QTimer>
#include <deque>

/**
 * @brief Rolling-window TCP health metrics: RTT (mean + jitter over RTT_WINDOW samples), audio
 *        packet loss (by sequence-gap detection), underrun count, buffer-fill snapshots, and a
 *        derived Green/Yellow/Orange/Red `HealthTier`. Drives `NetHealthWidget`.
 */
class NetworkMetrics : public QObject {
    Q_OBJECT
public:
    enum HealthTier { Green = 0, Yellow = 1, Orange = 2, Red = 3 };
    Q_ENUM(HealthTier)

    explicit NetworkMetrics(QObject *parent = nullptr);

    HealthTier healthTier() const { return m_tier; }
    int rttCurrent() const { return m_rttCurrent; }
    double rttAvg() const { return m_rttAvg; }
    double rttJitter() const { return m_rttJitter; }
    int bufferBytes() const { return m_bufferBytes; }
    int bufferMaxBytes() const { return m_bufferMaxBytes; }
    int underrunsTotal() const { return m_underrunsTotal; }
    int lostPacketsTotal() const { return m_lostPacketsTotal; }
    int packetRate() const { return m_totalPacketsInterval; }

public slots:
    void onLatencyChanged(int ms);
    void onAudioSequence(quint8 seq);
    void onBufferStatus(int queueBytes, int maxBytes, bool prebuffering);
    void onConnectionStateChanged(bool connected);

signals:
    void healthTierChanged(HealthTier tier);

private slots:
    void onSummaryTimer();

private:
    void computeHealthTier();
    void updateRttStats();

    QTimer *m_summaryTimer;

    // RTT tracking (sliding window)
    static constexpr int RTT_WINDOW = 30;
    std::deque<int> m_rttSamples;
    int m_rttCurrent = -1;
    double m_rttAvg = 0.0;
    double m_rttJitter = 0.0;

    // Packet loss tracking
    int m_lastAudioSeq = -1;
    int m_lostPacketsInterval = 0;
    int m_totalPacketsInterval = 0;
    int m_lostPacketsTotal = 0;
    int m_totalPacketsTotal = 0;

    // Audio buffer depth (latest snapshot).
    // 96000 bytes = 1.0 s at 12 kHz stereo Float32 (12000 × 2 × 4 = 96000). The 1 s ceiling
    // matches AudioEngine::MAX_QUEUE_BYTES so the health meter's denominator is meaningful.
    static constexpr int kDefaultBufferMaxBytes = 96000;
    int m_bufferBytes = 0;
    int m_bufferMaxBytes = kDefaultBufferMaxBytes;
    bool m_prebuffering = true;

    // Underrun counter
    int m_underrunsInterval = 0;
    int m_underrunsTotal = 0;

    // Audio activity tracking
    bool m_audioActive = false;    // Received packets this interval
    bool m_audioWasActive = false; // Received packets in a previous interval
    int m_stalePingCount = 0;      // Consecutive intervals with no new RTT sample

    // Connection state
    bool m_connected = false;

    // Health tier
    HealthTier m_tier = Red;
};

#endif // NETWORKMETRICS_H
