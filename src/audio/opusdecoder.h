#ifndef OPUSDECODER_H
#define OPUSDECODER_H

#include <QObject>
#include <opus/opus.h>

/**
 * @brief Opus decoder wrapper for inbound K4 audio packets. Handles K4's audio-packet framing
 *        (EM0..EM3) and emits stereo Float32 PCM. Volume/mix/balance is applied later in
 *        AudioEngine at playback time, not here.
 *
 *        Per-mode normalization + gain (verified empirically; see commit 24f4e45):
 *          EM0 (32-bit container, S16-range payload) — NORMALIZE_K4_RAW (1/2^17) only.
 *                                                       Accounts for K4's ~4× headroom
 *                                                       over nominal S16.
 *          EM1 (S16LE)                               — NORMALIZE_16BIT × K4_EM1_GAIN_BOOST (16×).
 *                                                       Compensates for K4 shipping EM1 at
 *                                                       ~-35 dBFS (~18× quieter than EM0).
 *          EM2 (Opus → S16)                          — NORMALIZE_16BIT × K4_GAIN_BOOST (32×).
 *          EM3 (Opus → float)                        — K4_GAIN_BOOST (32×) only.
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

    // S16 -> float32 normalization factor. Public because AudioController's TX
    // mic path (EM0 RAW float) uses the same conversion and was previously
    // duplicating this value locally.
    static constexpr float NORMALIZE_16BIT = 1.0f / 32768.0f;

private:
    ::OpusDecoder *m_decoder;
    int m_sampleRate;
    int m_channels;

    // Normalization constants
    // WHY: K4 RAW modes (EM0/EM1) ship samples with ~4× headroom over nominal S16
    // (empirical peaks up to ~131k = 2^17). Normalizing by 2^17 keeps transients
    // ≤ 1.0 in float so the ±1.0 clamp in AudioEngine never hard-clips them.
    static constexpr float NORMALIZE_K4_RAW = 1.0f / 131072.0f;

    // K4-specific gain boost for Opus modes (EM2/EM3).
    static constexpr float K4_GAIN_BOOST = 32.0f;

    // K4 EM1 (S16 RAW) gain boost. Empirical: K4 ships EM1 at ~-35 dBFS (peaks 480-600 in
    // qint16 across many seconds of audio), ~18× quieter than EM0. 16× brings typical
    // amplitude to ~0.26 in float, matching EM0's ~0.29 perceived loudness.
    static constexpr float K4_EM1_GAIN_BOOST = 16.0f;
};

#endif // OPUSDECODER_H
