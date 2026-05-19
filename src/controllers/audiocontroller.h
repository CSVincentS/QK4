#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QThread>

class AudioEngine;
class OpusDecoder;
class OpusEncoder;
class ConnectionController;
class RadioState;

/**
 * @brief Owns the audio thread + AudioEngine + Opus codecs. Task-level API over the RX/TX paths:
 *        startAudio/stopAudio, PTT toggle, atomic volume/mix/balance setters. Connects to
 *        RadioState.streamingLatencyChanged to resize TX Opus frames in step with the K4's SL tier.
 *
 * Threading:
 *   - AudioEngine is moved to `m_audioThread` (single moveToThread at construction).
 *   - OpusDecoder keeps main-thread affinity but is only called from the IO-thread lambda
 *     wired to Protocol::audioDataReady — effectively single-threaded on the IO thread.
 *   - OpusEncoder keeps main-thread affinity and is called from the audio thread via queued
 *     signals (Qt auto-connect resolves to QueuedConnection).
 *   Public AudioController methods are safe to call from the main thread (they dispatch via
 *   QMetaObject::invokeMethod / atomics).
 */
class AudioController : public QObject {
    Q_OBJECT

public:
    AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent = nullptr);
    ~AudioController();

    // Audio lifecycle
    void startAudio(float mainVolume, float subVolume, float micGain);
    void stopAudio();
    void shutdown();

    // PTT control
    void setPttActive(bool active);
    bool isPttActive() const { return m_pttActive; }

    // Volume/mix controls (atomic — safe from any thread)
    void setMainVolume(float vol);
    void setSubVolume(float vol);
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset);
    void setAudioMix(int left, int right);
    void setSubMuted(bool muted);

    // Device selection + mic gain — used by Options dialog tabs. Each dispatches via
    // QMetaObject::invokeMethod to the audio thread internally. Task-level API only —
    // per CONVENTIONS.md Rule 2, callers do not get direct access to AudioEngine.
    void setMicDevice(const QString &deviceId);
    void setOutputDevice(const QString &deviceId);
    void setMicGain(float gain); // 0.0 to 1.0

private slots:
    void onMicrophoneFrame(const QByteArray &s16leData);
    void onStreamingLatencyChanged(int tier);

private:
    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    OpusDecoder *m_opusDecoder;
    OpusEncoder *m_opusEncoder;

    bool m_pttActive = false;
    quint8 m_txSequence = 0;
    int m_txFrameSamples = 240; // Current TX frame size, matches SL tier
};

#endif // AUDIOCONTROLLER_H
