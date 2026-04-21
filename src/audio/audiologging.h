#ifndef AUDIO_AUDIOLOGGING_H
#define AUDIO_AUDIOLOGGING_H

#include <QLoggingCategory>

// Shared logging category for the audio subsystem (AudioController, AudioEngine,
// OpusDecoder, OpusEncoder, SidetoneGenerator). Definition lives in audiocontroller.cpp.
//
// Runtime filter example:
//     QT_LOGGING_RULES="qk4.audio.debug=false"
Q_DECLARE_LOGGING_CATEGORY(qk4Audio)

#endif // AUDIO_AUDIOLOGGING_H
