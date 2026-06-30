#include "networkmetrics.h"
#include <cmath>

namespace {
// Summary interval: how often we re-derive `HealthTier` and fire `healthTierChanged`. 2 s keeps
// the NetHealth LED responsive without thrashing consumers; must be ≥ ping interval so at least
// one RTT sample can land per interval (otherwise `m_stalePingCount` falsely trips Red).
constexpr int kSummaryIntervalMs = 2000;

// Buffer-fill thresholds for HealthTier, expressed in milliseconds of decoded audio (bytes / 96).
// Normal steady-state is 1-3 packets (20-60 ms depending on SL). Over 500 ms means packets queued
// during a network event and never flushed.
constexpr double kBufferMsWarnYellow = 200.0;
constexpr double kBufferMsWarnOrange = 500.0;
} // namespace

NetworkMetrics::NetworkMetrics(QObject *parent) : QObject(parent) {
    m_summaryTimer = new QTimer(this);
    m_summaryTimer->setInterval(kSummaryIntervalMs);
    connect(m_summaryTimer, &QTimer::timeout, this, &NetworkMetrics::onSummaryTimer);
    m_clock.start();
}

void NetworkMetrics::pushHistory(std::deque<TimedSample> &history, float value) {
    const qint64 now = m_clock.elapsed();
    history.push_back({now, value});
    while (!history.empty() &&
           (now - history.front().tMs > HISTORY_WINDOW_MS || static_cast<int>(history.size()) > HISTORY_MAX_SAMPLES))
        history.pop_front();
}

void NetworkMetrics::onLatencyChanged(int ms) {
    m_rttCurrent = ms;
    m_stalePingCount = 0;
    m_rttSamples.push_back(ms);
    if (static_cast<int>(m_rttSamples.size()) > RTT_WINDOW)
        m_rttSamples.pop_front();
    updateRttStats();
    // Trail RTT and the freshly recomputed jitter for the sparkline popup.
    pushHistory(m_rttHistory, static_cast<float>(ms));
    pushHistory(m_jitterHistory, static_cast<float>(m_rttJitter));
}

void NetworkMetrics::updateRttStats() {
    if (m_rttSamples.empty())
        return;

    double sum = 0.0;
    for (int s : m_rttSamples) {
        sum += s;
    }
    m_rttAvg = sum / m_rttSamples.size();

    // Jitter = stddev
    double variance = 0.0;
    for (int s : m_rttSamples) {
        double diff = s - m_rttAvg;
        variance += diff * diff;
    }
    m_rttJitter = std::sqrt(variance / m_rttSamples.size());
}

void NetworkMetrics::onAudioSequence(quint8 seq) {
    m_totalPacketsInterval++;
    m_totalPacketsTotal++;
    m_audioActive = true;

    if (m_lastAudioSeq >= 0) {
        int expected = (m_lastAudioSeq + 1) & 0xFF;
        if (seq != expected) {
            int gap = (seq - expected) & 0xFF;
            if (gap > 0 && gap < 128) {
                m_lostPacketsInterval += gap;
                m_lostPacketsTotal += gap;
            }
        }
    }
    m_lastAudioSeq = seq;
}

void NetworkMetrics::onBufferStatus(int queueBytes, int maxBytes, bool prebuffering) {
    m_bufferBytes = queueBytes;
    m_bufferMaxBytes = maxBytes;
    m_prebuffering = prebuffering;
    // 96 bytes = 1 ms of decoded audio (12 kHz stereo Float32); trail buffer depth in ms.
    pushHistory(m_bufferHistory, static_cast<float>(queueBytes) / 96.0f);
}

void NetworkMetrics::onConnectionStateChanged(bool connected) {
    m_connected = connected;
    if (connected) {
        m_summaryTimer->start();
        // Reset state for new connection
        m_rttSamples.clear();
        m_rttHistory.clear();
        m_jitterHistory.clear();
        m_bufferHistory.clear();
        m_rttCurrent = -1;
        m_rttAvg = 0.0;
        m_rttJitter = 0.0;
        m_lastAudioSeq = -1;
        m_lostPacketsInterval = 0;
        m_totalPacketsInterval = 0;
        m_lostPacketsTotal = 0;
        m_totalPacketsTotal = 0;
        m_underrunsInterval = 0;
        m_underrunsTotal = 0;
        m_prebuffering = true;
        m_audioActive = false;
        m_audioWasActive = false;
        m_stalePingCount = 0;
        m_tier = Green;
        emit healthTierChanged(m_tier);
    } else {
        m_summaryTimer->stop();
        m_audioActive = false;
        m_rttCurrent = -1;
        m_tier = Red;
        emit healthTierChanged(m_tier);
    }
}

void NetworkMetrics::computeHealthTier() {
    if (!m_connected) {
        if (m_tier != Red) {
            m_tier = Red;
            emit healthTierChanged(m_tier);
        }
        return;
    }

    HealthTier tier = Green;

    // RTT thresholds
    if (m_rttCurrent > 200)
        tier = std::max(tier, Red);
    else if (m_rttCurrent > 100)
        tier = std::max(tier, Orange);
    else if (m_rttCurrent > 50)
        tier = std::max(tier, Yellow);

    // RTT jitter
    if (m_rttJitter > 50.0)
        tier = std::max(tier, Orange);
    else if (m_rttJitter > 20.0)
        tier = std::max(tier, Yellow);

    // Buffer depth: high buffer = playing stale audio (latency accumulation).
    // Thresholds live in the anonymous namespace at file top.
    double bufferMs = m_bufferBytes / 96.0;
    if (bufferMs > kBufferMsWarnOrange)
        tier = std::max(tier, Orange);
    else if (bufferMs > kBufferMsWarnYellow)
        tier = std::max(tier, Yellow);

    // Audio dropout: was receiving packets but stopped
    if (m_audioWasActive && !m_audioActive)
        tier = std::max(tier, Orange);

    // Stale RTT: no PONG replies (ping sent every 1s, summary every 2s → expect ≥1)
    if (m_stalePingCount >= 2)
        tier = std::max(tier, Red);
    else if (m_stalePingCount >= 1)
        tier = std::max(tier, Orange);

    if (m_tier != tier) {
        m_tier = tier;
        emit healthTierChanged(m_tier);
    }
}

void NetworkMetrics::onSummaryTimer() {
    if (!m_connected)
        return;

    computeHealthTier();

    // Track stale ping (reset in onLatencyChanged when PONG arrives)
    m_stalePingCount++;

    // Reset per-interval counters
    m_lostPacketsInterval = 0;
    m_totalPacketsInterval = 0;
    m_underrunsInterval = 0;
    if (m_audioActive)
        m_audioWasActive = true;
    m_audioActive = false; // Will be set true again when next audio packet arrives
}
