# audio/

RX and TX audio pipeline. Owned by `controllers/audiocontroller.cpp`.

## Files

- `audioengine.{cpp,h}` — `QAudioSink` + `QAudioSource` with a jitter buffer (10ms feed timer, ~40ms prebuffer). Handles volume, balance, mix routing.
- `opusdecoder.{cpp,h}` — Opus decode wrapper. K4 sends 12kHz stereo (left=Main, right=Sub).
- `opusencoder.{cpp,h}` — Opus encode wrapper for TX. 12kHz mono; frame size reconfigured per K4 SL tier.
- `sidetonegenerator.{cpp,h}` — Real-time CW sidetone synthesis at 48kHz.

## Threading

- `AudioEngine` lives on `AudioController::m_audioThread` (moveToThread at construction).
- `OpusDecoder` has main-thread affinity but is only called from the IO-thread lambda wired to `Protocol::audioDataReady` — effectively single-threaded on the IO thread.
- `OpusEncoder` has main-thread affinity; called from the audio thread via queued signals.
- `SidetoneGenerator` lives on `HardwareController::m_sidetoneThread`.

## RX path

K4 → Protocol → OpusDecoder → AudioEngine::enqueueAudio → QAudioSink → speakers.

## TX path

Microphone → QAudioSource → AudioEngine → OpusEncoder → ConnectionController::sendAudio → K4.

## See also

- `memory/audio-architecture-plan.md` — jitter buffer design, buffering tradeoffs.
- `memory/audio-thread-plan.md` — thread migration details.
- `memory/k4-streaming-latency.md` — SL tier → frame size map.
