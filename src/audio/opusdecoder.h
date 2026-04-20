#ifndef OPUSDECODER_H
#define OPUSDECODER_H

#include <QObject>
#include <opus/opus.h>

/**
 * @brief Opus decoder wrapper for inbound K4 audio packets. Handles K4's audio-packet framing
 *        (EM0..EM3), applies the K4_GAIN_BOOST to quiet Opus/S32LE modes (EM1/S16LE already at
 *        full scale), and emits stereo Float32 PCM. Volume/mix/balance is applied later in
 *        AudioEngine at playback time, not here.
 */
class OpusDecoder : public QObject {
    Q_OBJECT

public:
    explicit OpusDecoder(QObject *parent = nullptr);
    ~OpusDecoder();

    // Initialize decoder (K4 uses 12000Hz stereo)
    bool initialize(int sampleRate = 12000, int channels = 2);

    // Decode K4 audio packet payload, returns raw normalized stereo Float32 PCM
    // Output is interleaved [main, sub, main, sub, ...] with gain boost applied
    // Volume/routing/balance is NOT applied here — that happens at playback time
    QByteArray decodeK4Packet(const QByteArray &packet);

    // Raw decode for testing (returns S16LE stereo PCM)
    QByteArray decode(const QByteArray &opusData);

    // Decode to float (returns float32 stereo PCM)
    QByteArray decodeFloat(const QByteArray &opusData);

private:
    ::OpusDecoder *m_decoder;
    int m_sampleRate;
    int m_channels;

    // Normalization constants
    static constexpr float NORMALIZE_16BIT = 1.0f / 32768.0f;
    static constexpr float NORMALIZE_32BIT = 1.0f / 2147483648.0f;

    // K4-specific gain boost (Opus and S32LE audio is very quiet)
    // Note: EM1 (S16LE RAW) is already at full scale and doesn't need boost
    static constexpr float K4_GAIN_BOOST = 32.0f;
};

#endif // OPUSDECODER_H
