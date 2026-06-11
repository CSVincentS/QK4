#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QtMath>
#include <atomic>

/**
 * @brief Local CW sidetone synth. Lives on its own thread (HardwareController's m_sidetoneThread)
 *        so audio output never blocks the keyer or UI. IambicKeyer drives `playSingleDit/Dah` per
 *        element for both HaliKey transports (V1.4 serial and MIDI); when the KPOD+ owns the CW
 *        path, CwController suppresses this local sidetone entirely. Each element is written as a
 *        complete PCM block (tone + inter-element space) and always plays to completion.
 *        All public slots are Q_INVOKABLE and expected to be posted via QueuedConnection.
 */
class SidetoneGenerator : public QObject {
    Q_OBJECT
public:
    explicit SidetoneGenerator(QObject *parent = nullptr);
    ~SidetoneGenerator();

    Q_INVOKABLE void setFrequency(int hz);
    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void setKeyerSpeed(int wpm);

    // Initialize/shutdown audio (called on sidetone thread via invokeMethod)
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    // Play a single element (one per IambicKeyer::elementStarted)
    Q_INVOKABLE void playSingleDit();
    Q_INVOKABLE void playSingleDah();

signals:

private:
    void initAudio();
    void playElement(int durationMs);
    int ditDurationMs() const;
    int dahDurationMs() const;

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_pushDevice = nullptr;
    QString m_selectedOutputDeviceId;
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    std::atomic<int> m_keyerWpm{20};
    double m_phase = 0.0;

    // Pre-allocated PCM scratch buffer. Worst case is 5 WPM dah (720 ms tone +
    // 240 ms inter-element space) at 48 kHz × 2 bytes = ~92 kB. Sized to
    // 128 kB so a future slower-keyer mode doesn't realloc. Previously
    // playElement() allocated a fresh QByteArray per CW element (7+ allocs/sec
    // during keying). resize() against the pre-sized capacity is alloc-free.
    QByteArray m_elementBuffer;
};

#endif // SIDETONEGENERATOR_H
