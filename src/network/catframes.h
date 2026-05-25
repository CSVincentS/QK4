#pragma once

#include "models/radiostate.h"

#include <QByteArray>

namespace CatFrames {

QByteArray frequencyA(quint64 hz);
QByteArray frequencyB(quint64 hz);

QByteArray modeA(RadioState::Mode m);
QByteArray modeB(RadioState::Mode m);

QByteArray ptt(bool transmitting);
QByteArray split(bool enabled);
QByteArray ritOffset(int offset);
QByteArray ritEnabled(bool en);
QByteArray xitEnabled(bool en);
QByteArray rfPower(double watts);
QByteArray rfPowerExtended(double watts, bool qrp);
QByteArray filterBandwidth(int bwHz);
QByteArray filterWidthExtended(int bwHz);
QByteArray keyerSpeed(int wpm);
QByteArray noiseBlanker(bool on);
QByteArray noiseReduction(bool on);
QByteArray agcSpeed(int agc);
QByteArray vox(bool on);
QByteArray diversity(bool on);
QByteArray dataSubMode(int subMode);
QByteArray aiMode(int level);

QByteArray ifFrame(const RadioState &state);

} // namespace CatFrames
