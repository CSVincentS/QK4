#include "radiostate.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <algorithm>

RadioState::RadioState(QObject *parent) : QObject(parent) {
    registerCommandHandlers();
}

void RadioState::reset() {
    // Frequency / VFO / RIT/XIT state — subsystem struct handles split + offsets too.
    m_frequencyVfoState.reset();
    // Data-mode + tuning step + streaming latency.
    m_dataControlState.reset();

    // Mode / filter / CW pitch / keyer config — subsystem struct.
    m_modeFilterState.reset();

    // Power and levels
    m_rfPower = -1.0;
    m_isQrpMode = false;
    m_micGain = -1;
    m_compression = -1;
    m_rfGain = -999;
    m_squelchLevel = -1;
    m_rfGainB = -999;
    m_squelchLevelB = -1;
    // Keyer speed / iambic / paddle / weight reset via m_modeFilterState.reset() above.

    // Meters
    m_sMeter = 0.0;
    m_sMeterB = 0.0;

    m_swrMeter = 1.0;
    m_alcMeter = 0;
    m_compressionDb = 0;
    m_forwardPower = 0.0;

    // Power supply
    m_supplyVoltage = 0.0;
    m_supplyCurrent = 0.0;

    // Control states
    m_isTransmitting = false;
    m_subReceiverEnabled = false;
    m_diversityEnabled = false;
    // splitEnabled reset via m_frequencyVfoState.reset() above.

    // Processing state (NB/NR/PA/RA/GT + NA/NM for both VFOs).
    m_processingState.reset();

    // Antenna
    m_antennaState.reset();

    // RIT/XIT state reset via m_frequencyVfoState.reset() above.

    // Message bank
    m_messageBank = -1;

    // Audio pipeline state (VOX/FX/AP/mix/balance/ML/LO/LI/MI/MS/RE/TE/VG/VI/ES).
    m_audioEffectsState.reset();

    // QSK / TEST / B SET
    m_qskEnabled = false;
    m_testMode = false;
    m_bSetEnabled = false;

    // QSK/VOX Delay
    m_qskDelayCW = -1;
    m_qskDelayVoice = -1;
    m_qskDelayData = -1;

    // VFO Link/Lock
    m_vfoLink = false;
    m_lockA = false;
    m_lockB = false;

    // Panadapter
    m_refLevel = -110;
    m_scale = -1;
    m_spanHz = 0;
    m_refLevelB = -110;
    m_spanHzB = 0;

    // Radio info
    m_radioID.clear();
    m_radioModel.clear();
    m_optionModules.clear();
    m_firmwareVersions.clear();

    // Mini-Pan
    m_miniPanAEnabled = false;
    m_miniPanBEnabled = false;

    // Display state
    m_dualPanModeLcd = -1;
    m_dualPanModeExt = -1;
    m_displayModeLcd = -1;
    m_displayModeExt = -1;
    m_displayFps = 30;
    m_waterfallColor = -1;
    m_waterfallHeight = 50;
    m_waterfallHeightExt = 50;
    m_averaging = -1;
    m_peakMode = -1;
    m_fixedTune = -1;
    m_fixedTuneMode = -1;
    m_freeze = -1;
    m_vfoACursor = -1;
    m_vfoBCursor = -1;
    m_autoRefLevel = -1;
    m_ddcNbMode = -1;
    m_ddcNbLevel = -1;

    // Data-mode + rate + optimistic cooldowns reset via m_dataControlState.reset().

    // EQ bands / Line In/Out / Mic Input/Setup / VOX Gain / Anti-VOX / ESSB
    // are reset via m_audioEffectsState.reset() above.

    // Antenna config masks reset as part of m_antennaState.reset() above.

    // Streaming latency reset via m_dataControlState.reset().

    // Text Decode
    m_textDecodeState.reset();
}

void RadioState::parseCATCommand(const QString &command) {
    // RadioState is not thread-safe — all callers must be on the main (GUI) thread.
    // If cross-thread parsing is ever needed, use QMetaObject::invokeMethod with Qt::QueuedConnection.
    Q_ASSERT(QThread::currentThread() == thread());

    QString cmd = command.trimmed();
    if (cmd.isEmpty())
        return;

    // Remove trailing semicolon for parsing
    if (cmd.endsWith(';')) {
        cmd.chop(1);
    }

    // Dispatch through handler registry (sorted by prefix length, longest first)
    for (const auto &entry : m_commandHandlers) {
        if (cmd.startsWith(entry.prefix)) {
            entry.handler(cmd);
            return;
        }
    }

    // Unknown command - no handler matched
    // qDebug() << "Unhandled CAT command:" << cmd;
}

// Legacy parseCATCommand content removed - now using handler registry above
// Old implementation was ~1500 lines of if-else chain

RadioState::Mode RadioState::modeFromCode(int code) {
    switch (code) {
    case 1:
        return LSB;
    case 2:
        return USB;
    case 3:
        return CW;
    case 4:
        return FM;
    case 5:
        return AM;
    case 6:
        return DATA;
    case 7:
        return CW_R;
    case 9:
        return DATA_R;
    default:
        return USB;
    }
}

QString RadioState::modeToString(Mode mode) {
    switch (mode) {
    case LSB:
        return "LSB";
    case USB:
        return "USB";
    case CW:
        return "CW";
    case FM:
        return "FM";
    case AM:
        return "AM";
    case DATA:
        return "DATA";
    case CW_R:
        return "CW-R";
    case DATA_R:
        return "DATA-R";
    case Unknown:
    default:
        return "";
    }
}

QString RadioState::modeString() const {
    return modeToString(mode());
}

QString RadioState::dataSubModeToString(int subMode) {
    switch (subMode) {
    case 0:
        return "DATA"; // DATA-A
    case 1:
        return "AFSK"; // AFSK-A
    case 2:
        return "FSK"; // FSK-D
    case 3:
        return "PSK"; // PSK-D
    default:
        return "DATA";
    }
}

QString RadioState::modeStringFull() const {
    // For DATA / DATA-R modes, show the sub-mode instead.
    const Mode m = mode();
    if (m == DATA || m == DATA_R)
        return dataSubModeToString(m_dataControlState.dataSubMode);
    return modeToString(m);
}

QString RadioState::modeStringFullB() const {
    const Mode m = modeB();
    if (m == DATA || m == DATA_R)
        return dataSubModeToString(m_dataControlState.dataSubModeB);
    return modeToString(m);
}

// Mode/filter/CW pitch/keyer optimistic setters — delegate to ModeFilterHandlers.
void RadioState::setKeyerSpeed(int wpm) {
    ModeFilterHandlers::setKeyerSpeed(m_modeFilterState, *this, wpm);
}
void RadioState::setCwPitch(int pitchHz) {
    ModeFilterHandlers::setCwPitch(m_modeFilterState, *this, pitchHz);
}

void RadioState::setRfPower(double watts) {
    bool qrp = (watts <= 10.0);
    bool changed = false;
    if (m_rfPower != watts) {
        m_rfPower = watts;
        changed = true;
    }
    if (qrp != m_isQrpMode) {
        m_isQrpMode = qrp;
        changed = true;
    }
    if (changed)
        emit rfPowerChanged(m_rfPower, m_isQrpMode);
}

void RadioState::setFilterBandwidth(int bwHz) {
    ModeFilterHandlers::setFilterBandwidth(m_modeFilterState, *this, bwHz);
}
void RadioState::setIfShift(int shift) {
    ModeFilterHandlers::setIfShift(m_modeFilterState, *this, shift);
}
void RadioState::setFilterBandwidthB(int bwHz) {
    ModeFilterHandlers::setFilterBandwidthB(m_modeFilterState, *this, bwHz);
}
void RadioState::setIfShiftB(int shift) {
    ModeFilterHandlers::setIfShiftB(m_modeFilterState, *this, shift);
}

void RadioState::setRfGain(int gain) {
    if (m_rfGain != gain) {
        m_rfGain = gain;
        emit rfGainChanged(m_rfGain);
    }
}

void RadioState::setSquelchLevel(int level) {
    if (m_squelchLevel != level) {
        m_squelchLevel = level;
        emit squelchChanged(m_squelchLevel);
    }
}

void RadioState::setRfGainB(int gain) {
    if (m_rfGainB != gain) {
        m_rfGainB = gain;
        emit rfGainBChanged(m_rfGainB);
    }
}

void RadioState::setSquelchLevelB(int level) {
    if (m_squelchLevelB != level) {
        m_squelchLevelB = level;
        emit squelchBChanged(m_squelchLevelB);
    }
}

void RadioState::setMicGain(int gain) {
    if (m_micGain != gain) {
        m_micGain = gain;
        emit micGainChanged(m_micGain);
    }
}

void RadioState::setCompression(int level) {
    if (m_compression != level) {
        m_compression = level;
        emit compressionChanged(m_compression);
    }
}

void RadioState::setBalance(int mode, int offset) {
    AudioEffectsHandlers::setBalance(m_audioEffectsState, *this, mode, offset);
}

void RadioState::setMonitorLevel(int mode, int level) {
    AudioEffectsHandlers::setMonitorLevel(m_audioEffectsState, *this, mode, level);
}

// Processing optimistic setters — delegate to ProcessingHandlers namespace.
void RadioState::setNoiseBlankerLevel(int level) {
    ProcessingHandlers::setNoiseBlankerLevel(m_processingState, *this, level);
}
void RadioState::setNoiseBlankerLevelB(int level) {
    ProcessingHandlers::setNoiseBlankerLevelB(m_processingState, *this, level);
}
void RadioState::setNoiseBlankerFilter(int filter) {
    ProcessingHandlers::setNoiseBlankerFilter(m_processingState, *this, filter);
}
void RadioState::setNoiseBlankerFilterB(int filter) {
    ProcessingHandlers::setNoiseBlankerFilterB(m_processingState, *this, filter);
}
void RadioState::setNoiseReductionLevel(int level) {
    ProcessingHandlers::setNoiseReductionLevel(m_processingState, *this, level);
}
void RadioState::setNoiseReductionLevelB(int level) {
    ProcessingHandlers::setNoiseReductionLevelB(m_processingState, *this, level);
}
void RadioState::setManualNotchPitch(int pitch) {
    ProcessingHandlers::setManualNotchPitch(m_processingState, *this, pitch);
}
void RadioState::setManualNotchPitchB(int pitch) {
    ProcessingHandlers::setManualNotchPitchB(m_processingState, *this, pitch);
}

// Data mode optimistic setters — delegate to DataControlHandlers namespace.
void RadioState::setDataSubMode(int subMode) {
    DataControlHandlers::setDataSubMode(m_dataControlState, *this, subMode);
}
void RadioState::setDataSubModeB(int subMode) {
    DataControlHandlers::setDataSubModeB(m_dataControlState, *this, subMode);
}
void RadioState::setDataRate(int rate) {
    DataControlHandlers::setDataRate(m_dataControlState, *this, rate);
}
void RadioState::setDataRateB(int rate) {
    DataControlHandlers::setDataRateB(m_dataControlState, *this, rate);
}

void RadioState::setMiniPanAEnabled(bool enabled) {
    if (m_miniPanAEnabled != enabled) {
        m_miniPanAEnabled = enabled;
        emit miniPanAEnabledChanged(enabled);
    }
}

void RadioState::setMiniPanBEnabled(bool enabled) {
    if (m_miniPanBEnabled != enabled) {
        m_miniPanBEnabled = enabled;
        emit miniPanBEnabledChanged(enabled);
    }
}

void RadioState::setWaterfallHeight(int percent) {
    percent = qBound(10, percent, 90);
    if (m_waterfallHeight != percent) {
        m_waterfallHeight = percent;
        emit waterfallHeightChanged(percent);
    }
}

void RadioState::setWaterfallHeightExt(int percent) {
    percent = qBound(10, percent, 90);
    if (m_waterfallHeightExt != percent) {
        m_waterfallHeightExt = percent;
        emit waterfallHeightExtChanged(percent);
    }
}

void RadioState::setAveraging(int value) {
    value = qBound(1, value, 20);
    if (m_averaging != value) {
        m_averaging = value;
        emit averagingChanged(value);
    }
}

void RadioState::setRxEqBand(int index, int dB) {
    AudioEffectsHandlers::setRxEqBand(m_audioEffectsState, *this, index, dB);
}

void RadioState::setRxEqBands(const QVector<int> &bands) {
    AudioEffectsHandlers::setRxEqBands(m_audioEffectsState, *this, bands);
}

void RadioState::setTxEqBand(int index, int dB) {
    AudioEffectsHandlers::setTxEqBand(m_audioEffectsState, *this, index, dB);
}

void RadioState::setTxEqBands(const QVector<int> &bands) {
    AudioEffectsHandlers::setTxEqBands(m_audioEffectsState, *this, bands);
}

// Antenna config setters — delegate to AntennaHandlers namespace.
void RadioState::setMainRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setMainRxAntConfig(m_antennaState, *this, displayAll, mask);
}
void RadioState::setSubRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setSubRxAntConfig(m_antennaState, *this, displayAll, mask);
}
void RadioState::setTxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setTxAntConfig(m_antennaState, *this, displayAll, mask);
}

void RadioState::setLineOutLeft(int level) {
    AudioEffectsHandlers::setLineOutLeft(m_audioEffectsState, *this, level);
}

void RadioState::setLineOutRight(int level) {
    AudioEffectsHandlers::setLineOutRight(m_audioEffectsState, *this, level);
}

void RadioState::setLineOutRightEqualsLeft(bool enabled) {
    AudioEffectsHandlers::setLineOutRightEqualsLeft(m_audioEffectsState, *this, enabled);
}

// Line In optimistic setters
void RadioState::setLineInSoundCard(int level) {
    AudioEffectsHandlers::setLineInSoundCard(m_audioEffectsState, *this, level);
}

void RadioState::setLineInJack(int level) {
    AudioEffectsHandlers::setLineInJack(m_audioEffectsState, *this, level);
}

void RadioState::setLineInSource(int source) {
    AudioEffectsHandlers::setLineInSource(m_audioEffectsState, *this, source);
}

// Mic Input/Setup optimistic setters
void RadioState::setMicInput(int input) {
    AudioEffectsHandlers::setMicInput(m_audioEffectsState, *this, input);
}

void RadioState::setMicFrontPreamp(int preamp) {
    AudioEffectsHandlers::setMicFrontPreamp(m_audioEffectsState, *this, preamp);
}

void RadioState::setMicFrontBias(int bias) {
    AudioEffectsHandlers::setMicFrontBias(m_audioEffectsState, *this, bias);
}

void RadioState::setMicFrontButtons(int buttons) {
    AudioEffectsHandlers::setMicFrontButtons(m_audioEffectsState, *this, buttons);
}

void RadioState::setMicRearPreamp(int preamp) {
    AudioEffectsHandlers::setMicRearPreamp(m_audioEffectsState, *this, preamp);
}

void RadioState::setMicRearBias(int bias) {
    AudioEffectsHandlers::setMicRearBias(m_audioEffectsState, *this, bias);
}

// Text Decode optimistic setters — delegate into the TextDecodeHandlers
// namespace (see models/radiostate/textdecodestate.cpp). Public API shape
// preserved; the struct holds the state and the handler fns mutate it.
void RadioState::setTextDecodeMode(int mode) {
    TextDecodeHandlers::setMode(m_textDecodeState, *this, mode);
}
void RadioState::setTextDecodeThreshold(int threshold) {
    TextDecodeHandlers::setThreshold(m_textDecodeState, *this, threshold);
}
void RadioState::setTextDecodeLines(int lines) {
    TextDecodeHandlers::setLines(m_textDecodeState, *this, lines);
}
void RadioState::setTextDecodeModeB(int mode) {
    TextDecodeHandlers::setModeB(m_textDecodeState, *this, mode);
}
void RadioState::setTextDecodeThresholdB(int threshold) {
    TextDecodeHandlers::setThresholdB(m_textDecodeState, *this, threshold);
}
void RadioState::setTextDecodeLinesB(int lines) {
    TextDecodeHandlers::setLinesB(m_textDecodeState, *this, lines);
}

// VOX Gain / Anti-VOX / ESSB optimistic setters — delegate into AudioEffectsHandlers.
void RadioState::setVoxGainVoice(int gain) {
    AudioEffectsHandlers::setVoxGainVoice(m_audioEffectsState, *this, gain);
}

void RadioState::setVoxGainData(int gain) {
    AudioEffectsHandlers::setVoxGainData(m_audioEffectsState, *this, gain);
}

void RadioState::setAntiVox(int level) {
    AudioEffectsHandlers::setAntiVox(m_audioEffectsState, *this, level);
}

void RadioState::setEssbEnabled(bool enabled) {
    AudioEffectsHandlers::setEssbEnabled(m_audioEffectsState, *this, enabled);
}

void RadioState::setSsbTxBw(int bw) {
    AudioEffectsHandlers::setSsbTxBw(m_audioEffectsState, *this, bw);
}

// =============================================================================
// A/B Deduplication Helpers
// =============================================================================

void RadioState::handleIntPair(const QString &cmd, int prefixLen, int &member, int min, int max,
                               void (RadioState::*signal)(int)) {
    if (cmd.length() <= prefixLen)
        return;
    bool ok;
    int val = cmd.mid(prefixLen).toInt(&ok);
    if (ok && val >= min && val <= max && val != member) {
        member = val;
        emit(this->*signal)(val);
    }
}

void RadioState::handleBoolPair(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)()) {
    if (cmd.length() <= charPos)
        return;
    bool enabled = (cmd.at(charPos) == '1');
    if (member != enabled) {
        member = enabled;
        emit(this->*signal)();
    }
}

void RadioState::handleBoolPairVal(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)(bool)) {
    if (cmd.length() <= charPos)
        return;
    bool enabled = (cmd.at(charPos) == '1');
    if (member != enabled) {
        member = enabled;
        emit(this->*signal)(enabled);
    }
}

// =============================================================================
// Command Handler Registry
// =============================================================================
//
// WHY a registry and not a giant switch:
//   1. K4 CAT prefixes are variable-length (2–5 chars: `FA`, `MD$`, `#REF$`). A switch on the
//      leading two characters would mis-route `MD$` into the `MD` branch — `$`-suffixed sub-RX
//      variants are indistinguishable from the base command at the first two bytes.
//   2. Prefix matching lets us keep the sub-RX variant (`$` suffix) alongside its base command
//      in code, which makes it obvious they share parsing logic (many use `handleIntPair()`).
//   3. The list is sorted longest-first so the dispatcher's first-match-wins rule correctly
//      prefers `RO$` over `RO` and `#REF$` over `#REF`.
//
// Dispatch is O(handlers) linear scan — with ~200 handlers and a single CAT stream, this has
// never been the bottleneck and keeping the list readable (grouped by subsystem) matters more
// than O(log n) matching.
//
// Pure-data and state fields that use `handleIntPair` consolidate the base and sub-RX variants
// into one call site; handlers that differ between main/sub (e.g., `MD` vs `MD$` often need to
// emit different signals) stay as separate lambdas.

QStringList RadioState::registeredCommandPrefixes() const {
    QStringList prefixes;
    prefixes.reserve(m_commandHandlers.size());
    for (const auto &entry : m_commandHandlers)
        prefixes.append(entry.prefix);
    return prefixes;
}

void RadioState::registerCommandHandlers() {
    // Register handlers in order - will be sorted by prefix length (longest first)
    // This ensures "MD$" is checked before "MD", "NB$" before "NB", etc.

    // Display commands (# prefix) - register longest first
    m_commandHandlers.append({"#HWFH", [this](const QString &c) { handleDisplayHWFH(c); }});
    m_commandHandlers.append({"#HDPM", [this](const QString &c) { handleDisplayHDPM(c); }});
    m_commandHandlers.append({"#HDSM", [this](const QString &c) { handleDisplayHDSM(c); }});
    m_commandHandlers.append({"#NBL$", [this](const QString &c) { handleDisplayNBL(c); }});
    // #REF$/#REF — deduplicated via handleIntPair (prefixLen 5 vs 4)
    m_commandHandlers.append({"#REF$", [this](const QString &c) {
                                  handleIntPair(c, 5, m_refLevelB, -200, 50, &RadioState::refLevelBChanged);
                              }});
    // #SPN$/#SPN — deduplicated via handleIntPair (prefixLen 5 vs 4, min 1)
    m_commandHandlers.append(
        {"#SPN$", [this](const QString &c) { handleIntPair(c, 5, m_spanHzB, 1, 999999, &RadioState::spanBChanged); }});
    m_commandHandlers.append({"#NB$", [this](const QString &c) { handleDisplayNB(c); }});
    // #MP$/#MP — deduplicated via handleBoolPairVal (charPos 4 vs 3)
    m_commandHandlers.append({"#MP$", [this](const QString &c) {
                                  handleBoolPairVal(c, 4, m_miniPanBEnabled, &RadioState::miniPanBEnabledChanged);
                              }});
    m_commandHandlers.append({"#REF", [this](const QString &c) {
                                  handleIntPair(c, 4, m_refLevel, -200, 50, &RadioState::refLevelChanged);
                              }});
    m_commandHandlers.append(
        {"#SPN", [this](const QString &c) { handleIntPair(c, 4, m_spanHz, 1, 999999, &RadioState::spanChanged); }});
    m_commandHandlers.append({"#SCL", [this](const QString &c) { handleDisplaySCL(c); }});
    m_commandHandlers.append({"#DPM", [this](const QString &c) { handleDisplayDPM(c); }});
    m_commandHandlers.append({"#DSM", [this](const QString &c) { handleDisplayDSM(c); }});
    m_commandHandlers.append({"#FPS", [this](const QString &c) { handleDisplayFPS(c); }});
    m_commandHandlers.append({"#WFC", [this](const QString &c) { handleDisplayWFC(c); }});
    m_commandHandlers.append({"#WFH", [this](const QString &c) { handleDisplayWFH(c); }});
    m_commandHandlers.append({"#AVG", [this](const QString &c) { handleDisplayAVG(c); }});
    m_commandHandlers.append({"#PKM", [this](const QString &c) { handleDisplayPKM(c); }});
    m_commandHandlers.append({"#FXT", [this](const QString &c) { handleDisplayFXT(c); }});
    m_commandHandlers.append({"#FXA", [this](const QString &c) { handleDisplayFXA(c); }});
    m_commandHandlers.append({"#FRZ", [this](const QString &c) { handleDisplayFRZ(c); }});
    m_commandHandlers.append({"#VFA", [this](const QString &c) { handleDisplayVFA(c); }});
    m_commandHandlers.append({"#VFB", [this](const QString &c) { handleDisplayVFB(c); }});
    m_commandHandlers.append({"#MP", [this](const QString &c) {
                                  handleBoolPairVal(c, 3, m_miniPanAEnabled, &RadioState::miniPanAEnabledChanged);
                              }});
    m_commandHandlers.append({"#AR", [this](const QString &c) { handleDisplayAR(c); }});

    // Multi-char commands with $ suffix (must come before their base commands)
    m_commandHandlers.append({"SIFP", [this](const QString &c) { handleSIFP(c); }});

    m_commandHandlers.append({"TD$", [this](const QString &c) { handleTDSub(c); }});
    m_commandHandlers.append({"TB$", [this](const QString &c) { handleTBSub(c); }});
    m_commandHandlers.append({"DR$", [this](const QString &c) { handleDRSub(c); }});
    m_commandHandlers.append({"DT$", [this](const QString &c) { handleDTSub(c); }});
    m_commandHandlers.append({"MD$", [this](const QString &c) { handleMDSub(c); }});
    // BW$, IS$, FP$ — deduplicated via handleIntPair / ModeFilterHandlers.
    m_commandHandlers.append(
        {"BW$", [this](const QString &c) { ModeFilterHandlers::handleBWSub(m_modeFilterState, *this, c); }});
    m_commandHandlers.append({"IS$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_modeFilterState.ifShiftB, -99999, 99999,
                                                &RadioState::ifShiftBChanged);
                              }});
    m_commandHandlers.append({"FP$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_modeFilterState.filterPositionB, 1, 3,
                                                &RadioState::filterPositionBChanged);
                              }});
    // RG$ — strips leading dash before parsing
    // WHY: K4 reports RF gain as an attenuation in the range -0..-60 dB
    // ("RG-030;" = -30 dB). Throughout the codebase (SideControlPanel UI,
    // MainWindow scroll handlers, setRfGain()) RF gain is carried as a
    // positive *magnitude* 0..60; the minus sign is re-added only when
    // dispatching CAT or formatting for display. We take qAbs() here to
    // keep the magnitude convention explicit.
    m_commandHandlers.append({"RG$", [this](const QString &c) {
                                  if (c.length() <= 3)
                                      return;
                                  bool ok;
                                  int rg = qAbs(c.mid(3).toInt(&ok));
                                  if (ok && m_rfGainB != rg) {
                                      m_rfGainB = rg;
                                      emit rfGainBChanged(m_rfGainB);
                                  }
                              }});
    m_commandHandlers.append({"SQ$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_squelchLevelB, -99999, 99999, &RadioState::squelchBChanged);
                              }});
    m_commandHandlers.append({"SM$", [this](const QString &c) { handleSMSub(c); }});
    m_commandHandlers.append({"NB$", [this](const QString &c) { handleNBSub(c); }});
    m_commandHandlers.append({"NR$", [this](const QString &c) { handleNRSub(c); }});
    m_commandHandlers.append({"PA$", [this](const QString &c) { handlePASub(c); }});
    m_commandHandlers.append({"RA$", [this](const QString &c) { handleRASub(c); }});
    m_commandHandlers.append({"GT$", [this](const QString &c) { handleGTSub(c); }});
    // NA$ — deduplicated via handleBoolPair
    m_commandHandlers.append({"NA$", [this](const QString &c) {
                                  handleBoolPair(c, 3, m_processingState.autoNotchEnabledB, &RadioState::notchBChanged);
                              }});
    m_commandHandlers.append({"NM$", [this](const QString &c) { handleNMSub(c); }});
    m_commandHandlers.append({"AP$", [this](const QString &c) { handleAPSub(c); }});
    // LK$ — deduplicated via handleBoolPairVal
    m_commandHandlers.append(
        {"LK$", [this](const QString &c) { handleBoolPairVal(c, 3, m_lockB, &RadioState::lockBChanged); }});
    // VT$ — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append(
        {"VT$", [this](const QString &c) { DataControlHandlers::handleVTSub(m_dataControlState, *this, c); }});
    // AR$ — deduplicated inline (3-arg signal)
    m_commandHandlers.append(
        {"AR$", [this](const QString &c) { AntennaHandlers::handleARSub(m_antennaState, *this, c); }});
    // RO$ — inline (custom multi-arg signal).
    // WHY a separate RO$ handler (RIT/XIT offset routing on the K4):
    //   - No split, RIT or XIT active  → offset lives in `RO`  (VFO A, handled below)
    //   - Split + XIT                  → offset lives in `RO$` (VFO B, the TX VFO when split)
    //   - BSET + RIT                   → offset lives in `RO$` (VFO B)
    // We update `m_ritXitOffsetB` and emit the `B`-variant signal so the VFO B display tracks.
    // See `~/.claude/projects/.../memory/MEMORY.md` → "K4 RIT/XIT Offset Registers".
    m_commandHandlers.append(
        {"RO$", [this](const QString &c) { FrequencyVfoHandlers::handleROSub(m_frequencyVfoState, *this, c); }});
    m_commandHandlers.append(
        {"RT$", [this](const QString &c) { FrequencyVfoHandlers::handleRTSub(m_frequencyVfoState, *this, c); }});

    // 3-char commands
    m_commandHandlers.append({"ACN", [this](const QString &c) { handleACN(c); }});
    m_commandHandlers.append({"ACM", [this](const QString &c) { handleACM(c); }});
    m_commandHandlers.append({"ACS", [this](const QString &c) { handleACS(c); }});
    m_commandHandlers.append({"ACT", [this](const QString &c) { handleACT(c); }});
    m_commandHandlers.append({"RV.", [this](const QString &c) { handleRV(c); }});

    // 2-char base commands
    m_commandHandlers.append({"FA", [this](const QString &c) { handleFA(c); }});
    m_commandHandlers.append({"FB", [this](const QString &c) { handleFB(c); }});
    m_commandHandlers.append({"FT", [this](const QString &c) { handleFT(c); }});
    // FP — deduplicated via handleIntPair.
    m_commandHandlers.append({"FP", [this](const QString &c) {
                                  handleIntPair(c, 2, m_modeFilterState.filterPosition, 1, 3,
                                                &RadioState::filterPositionChanged);
                              }});
    m_commandHandlers.append({"FX", [this](const QString &c) { handleFX(c); }});
    m_commandHandlers.append({"MD", [this](const QString &c) { handleMD(c); }});
    m_commandHandlers.append({"BL", [this](const QString &c) { handleBL(c); }});
    // BW — deduplicated via ModeFilterHandlers (×10 multiplier).
    m_commandHandlers.append(
        {"BW", [this](const QString &c) { ModeFilterHandlers::handleBW(m_modeFilterState, *this, c); }});
    // IS — deduplicated via handleIntPair.
    m_commandHandlers.append({"IS", [this](const QString &c) {
                                  handleIntPair(c, 2, m_modeFilterState.ifShift, -99999, 99999,
                                                &RadioState::ifShiftChanged);
                              }});
    m_commandHandlers.append({"CW", [this](const QString &c) { handleCW(c); }});
    // RG — deduplicated inline (strips leading dash)
    // WHY: see RG$ handler above — RF gain is carried as positive magnitude 0..60.
    m_commandHandlers.append({"RG", [this](const QString &c) {
                                  if (c.length() <= 2)
                                      return;
                                  bool ok;
                                  int rg = qAbs(c.mid(2).toInt(&ok));
                                  if (ok && m_rfGain != rg) {
                                      m_rfGain = rg;
                                      emit rfGainChanged(m_rfGain);
                                  }
                              }});
    // SQ — deduplicated via handleIntPair
    m_commandHandlers.append({"SQ", [this](const QString &c) {
                                  handleIntPair(c, 2, m_squelchLevel, -99999, 99999, &RadioState::squelchChanged);
                              }});
    m_commandHandlers.append({"MG", [this](const QString &c) { handleMG(c); }});
    m_commandHandlers.append({"ML", [this](const QString &c) { handleML(c); }});
    m_commandHandlers.append({"CP", [this](const QString &c) { handleCP(c); }});
    m_commandHandlers.append({"PC", [this](const QString &c) { handlePC(c); }});
    m_commandHandlers.append({"KS", [this](const QString &c) { handleKS(c); }});
    m_commandHandlers.append({"KP", [this](const QString &c) { handleKP(c); }});
    m_commandHandlers.append({"SM", [this](const QString &c) { handleSM(c); }});
    m_commandHandlers.append({"PO", [this](const QString &c) { handlePO(c); }});
    m_commandHandlers.append({"TM", [this](const QString &c) { handleTM(c); }});
    m_commandHandlers.append({"TX", [this](const QString &c) { handleTX(c); }});
    m_commandHandlers.append({"RX", [this](const QString &c) { handleRX(c); }});
    m_commandHandlers.append({"NB", [this](const QString &c) { handleNB(c); }});
    m_commandHandlers.append({"NR", [this](const QString &c) { handleNR(c); }});
    // NA — deduplicated via handleBoolPair
    m_commandHandlers.append({"NA", [this](const QString &c) {
                                  handleBoolPair(c, 2, m_processingState.autoNotchEnabled, &RadioState::notchChanged);
                              }});
    m_commandHandlers.append({"NM", [this](const QString &c) { handleNM(c); }});
    m_commandHandlers.append({"PA", [this](const QString &c) { handlePA(c); }});
    m_commandHandlers.append({"RA", [this](const QString &c) { handleRA(c); }});
    m_commandHandlers.append({"GT", [this](const QString &c) { handleGT(c); }});
    m_commandHandlers.append({"AP", [this](const QString &c) { handleAP(c); }});
    m_commandHandlers.append({"LN", [this](const QString &c) { handleLN(c); }});
    // LK — deduplicated via handleBoolPairVal
    m_commandHandlers.append(
        {"LK", [this](const QString &c) { handleBoolPairVal(c, 2, m_lockA, &RadioState::lockAChanged); }});
    m_commandHandlers.append({"LO", [this](const QString &c) { handleLO(c); }});
    m_commandHandlers.append({"LI", [this](const QString &c) { handleLI(c); }});
    // VT — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append(
        {"VT", [this](const QString &c) { DataControlHandlers::handleVT(m_dataControlState, *this, c); }});
    m_commandHandlers.append({"VX", [this](const QString &c) { handleVX(c); }});
    m_commandHandlers.append({"VG", [this](const QString &c) { handleVG(c); }});
    m_commandHandlers.append({"VI", [this](const QString &c) { handleVI(c); }});
    m_commandHandlers.append({"MI", [this](const QString &c) { handleMI(c); }});
    m_commandHandlers.append({"MS", [this](const QString &c) { handleMS(c); }});
    m_commandHandlers.append({"MX", [this](const QString &c) { handleMX(c); }});
    m_commandHandlers.append({"MN", [this](const QString &c) { handleMN(c); }});
    m_commandHandlers.append({"ES", [this](const QString &c) { handleES(c); }});
    m_commandHandlers.append({"SD", [this](const QString &c) { handleSD(c); }});
    m_commandHandlers.append({"SL", [this](const QString &c) { handleSL(c); }});
    m_commandHandlers.append({"SB", [this](const QString &c) { handleSB(c); }});
    m_commandHandlers.append({"DV", [this](const QString &c) { handleDV(c); }});
    m_commandHandlers.append({"DR", [this](const QString &c) { handleDR(c); }});
    m_commandHandlers.append({"DT", [this](const QString &c) { handleDT(c); }});
    m_commandHandlers.append({"TS", [this](const QString &c) { handleTS(c); }});
    m_commandHandlers.append({"TB", [this](const QString &c) { handleTB(c); }});
    m_commandHandlers.append({"TD", [this](const QString &c) { handleTD(c); }});
    m_commandHandlers.append({"TE", [this](const QString &c) { handleTE(c); }});
    m_commandHandlers.append({"BS", [this](const QString &c) { handleBS(c); }});
    m_commandHandlers.append({"AN", [this](const QString &c) { handleAN(c); }});
    // AR — deduplicated inline (3-arg signal)
    m_commandHandlers.append({"AR", [this](const QString &c) { AntennaHandlers::handleAR(m_antennaState, *this, c); }});
    m_commandHandlers.append({"AT", [this](const QString &c) { handleAT(c); }});
    m_commandHandlers.append({"RT", [this](const QString &c) { handleRT(c); }});
    m_commandHandlers.append({"XT", [this](const QString &c) { handleXT(c); }});
    m_commandHandlers.append({"RO", [this](const QString &c) { handleRO(c); }});
    m_commandHandlers.append({"RE", [this](const QString &c) { handleRE(c); }});
    m_commandHandlers.append({"ID", [this](const QString &c) { handleID(c); }});
    m_commandHandlers.append({"OM", [this](const QString &c) { handleOM(c); }});
    m_commandHandlers.append({"ER", [this](const QString &c) { handleER(c); }});

    // Sort by prefix length (longest first) for correct matching
    std::sort(m_commandHandlers.begin(), m_commandHandlers.end(),
              [](const CommandEntry &a, const CommandEntry &b) { return a.prefix.length() > b.prefix.length(); });
}

// =============================================================================
// Individual Command Handlers - VFO/Frequency
// =============================================================================

// Frequency / VFO CAT handlers — delegate to FrequencyVfoHandlers namespace.
void RadioState::handleFA(const QString &cmd) {
    FrequencyVfoHandlers::handleFA(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleFB(const QString &cmd) {
    FrequencyVfoHandlers::handleFB(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleFT(const QString &cmd) {
    FrequencyVfoHandlers::handleFT(m_frequencyVfoState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Mode
// =============================================================================

void RadioState::handleMD(const QString &cmd) {
    ModeFilterHandlers::handleMD(m_modeFilterState, *this, cmd);
}
void RadioState::handleMDSub(const QString &cmd) {
    ModeFilterHandlers::handleMDSub(m_modeFilterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Bandwidth/Filter
// =============================================================================

void RadioState::handleCW(const QString &cmd) {
    ModeFilterHandlers::handleCW(m_modeFilterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Gain/Level
// =============================================================================

void RadioState::handleMG(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int gain = cmd.mid(2).toInt(&ok);
    if (ok && gain != m_micGain) {
        m_micGain = gain;
        emit micGainChanged(m_micGain);
    }
}

void RadioState::handleCP(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int comp = cmd.mid(2).toInt(&ok);
    if (ok && comp != m_compression) {
        m_compression = comp;
        emit compressionChanged(m_compression);
    }
}

void RadioState::handleML(const QString &cmd) {
    AudioEffectsHandlers::handleML(m_audioEffectsState, *this, cmd);
}

void RadioState::handleBL(const QString &cmd) {
    AudioEffectsHandlers::handleBL(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMX(const QString &cmd) {
    AudioEffectsHandlers::handleMX(m_audioEffectsState, *this, cmd);
}

void RadioState::handlePC(const QString &cmd) {
    // Power Control - PCnnnr; where nnn=power value, r=L/H/X
    // L = QRP (0.1-10W): nnn is watts*10 (e.g., 099 = 9.9W)
    // H = QRO (1-110W): nnn is watts directly (e.g., 050 = 50W)
    // X = XVTR (0.1-10mW): nnn is mW*10
    if (cmd.length() < 6)
        return;

    bool ok;
    int powerRaw = cmd.mid(2, 3).toInt(&ok);
    if (!ok)
        return;

    QChar mode = cmd.at(5);
    double watts;
    bool qrp;

    if (mode == 'L') {
        // QRP mode: value is watts * 10
        watts = powerRaw / 10.0;
        qrp = true;
    } else if (mode == 'H') {
        // QRO mode: value is watts directly
        watts = powerRaw;
        qrp = false;
    } else {
        // Unknown mode (X for XVTR, etc.) - skip
        return;
    }

    bool changed = false;
    if (watts != m_rfPower) {
        m_rfPower = watts;
        changed = true;
    }
    if (qrp != m_isQrpMode) {
        m_isQrpMode = qrp;
        changed = true;
    }
    if (changed) {
        emit rfPowerChanged(m_rfPower, m_isQrpMode);
    }
}

void RadioState::handleKS(const QString &cmd) {
    ModeFilterHandlers::handleKS(m_modeFilterState, *this, cmd);
}
void RadioState::handleKP(const QString &cmd) {
    ModeFilterHandlers::handleKP(m_modeFilterState, *this, cmd);
}

void RadioState::setIambicMode(QChar mode) {
    ModeFilterHandlers::setIambicMode(m_modeFilterState, *this, mode);
}
void RadioState::setPaddleOrientation(QChar orientation) {
    ModeFilterHandlers::setPaddleOrientation(m_modeFilterState, *this, orientation);
}
void RadioState::setKeyingWeight(int weight) {
    ModeFilterHandlers::setKeyingWeight(m_modeFilterState, *this, weight);
}

// =============================================================================
// Individual Command Handlers - Meters
// =============================================================================

void RadioState::handleSM(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int bars = cmd.mid(2).toInt(&ok);
    if (ok) {
        if (bars <= 18) {
            m_sMeter = bars / 2.0;
        } else {
            int dbAboveS9 = (bars - 18) * 3;
            m_sMeter = 9.0 + dbAboveS9 / 10.0;
        }
        emit sMeterChanged(m_sMeter);
    }
}

void RadioState::handleSMSub(const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    int bars = cmd.mid(3).toInt(&ok);
    if (ok) {
        if (bars <= 18) {
            m_sMeterB = bars / 2.0;
        } else {
            int dbAboveS9 = (bars - 18) * 3;
            m_sMeterB = 9.0 + dbAboveS9 / 10.0;
        }
        emit sMeterBChanged(m_sMeterB);
    }
}

void RadioState::handlePO(const QString &cmd) {
    Q_UNUSED(cmd)
    // PO (raw power meter) is received but unused — forward power
    // is already available through the TM handler's txMeterChanged signal.
}

void RadioState::handleTM(const QString &cmd) {
    // TX Meter Data (TM) - TMaaabbbcccddd; (ALC, CMP, FWD, SWR) - 3-digit fields
    if (cmd.length() < 14)
        return;
    QString data = cmd.mid(2);
    if (data.length() < 12)
        return;

    bool ok1, ok2, ok3, ok4;
    int alc = data.mid(0, 3).toInt(&ok1);
    int cmp = data.mid(3, 3).toInt(&ok2);
    int fwd = data.mid(6, 3).toInt(&ok3);
    int swrRaw = data.mid(9, 3).toInt(&ok4);

    if (!ok1 || !ok2 || !ok3 || !ok4)
        return;

    m_alcMeter = alc;
    m_compressionDb = cmp;
    // FWD is watts in QRO, tenths in QRP
    m_forwardPower = m_isQrpMode ? fwd / 10.0 : fwd;
    m_swrMeter = swrRaw / 10.0; // SWR in 1/10th units

    emit txMeterChanged(m_alcMeter, m_compressionDb, m_forwardPower, m_swrMeter);
    emit swrChanged(m_swrMeter);
}

// =============================================================================
// Individual Command Handlers - TX/RX State
// =============================================================================

void RadioState::handleTX(const QString &cmd) {
    Q_UNUSED(cmd)
    if (!m_isTransmitting) {
        m_isTransmitting = true;
        emit transmitStateChanged(true);
    }
}

void RadioState::handleRX(const QString &cmd) {
    Q_UNUSED(cmd)
    if (m_isTransmitting) {
        m_isTransmitting = false;
        emit transmitStateChanged(false);
    }
}

// =============================================================================
// Individual Command Handlers - Processing (NB, NR, PA, RA, GT, NA, NM)
// =============================================================================

// WHY: Handlers in this block (NB/NB$, NR/NR$, PA/PA$, RA/RA$, GT/GT$)
// all emit the cross-cutting `processingChanged()` / `processingChangedB()`
// rollup signal. Compute proposed values first, short-circuit if nothing
// differs, and only then mutate + emit — keeps the rollup idempotent so
// redundant CAT echoes don't trigger spurious UI repaints.

// Processing CAT handlers — delegate to ProcessingHandlers namespace.
void RadioState::handleNB(const QString &cmd) {
    ProcessingHandlers::handleNB(m_processingState, *this, cmd);
}
void RadioState::handleNBSub(const QString &cmd) {
    ProcessingHandlers::handleNBSub(m_processingState, *this, cmd);
}
void RadioState::handleNR(const QString &cmd) {
    ProcessingHandlers::handleNR(m_processingState, *this, cmd);
}
void RadioState::handleNRSub(const QString &cmd) {
    ProcessingHandlers::handleNRSub(m_processingState, *this, cmd);
}
void RadioState::handlePA(const QString &cmd) {
    ProcessingHandlers::handlePA(m_processingState, *this, cmd);
}
void RadioState::handlePASub(const QString &cmd) {
    ProcessingHandlers::handlePASub(m_processingState, *this, cmd);
}
void RadioState::handleRA(const QString &cmd) {
    ProcessingHandlers::handleRA(m_processingState, *this, cmd);
}
void RadioState::handleRASub(const QString &cmd) {
    ProcessingHandlers::handleRASub(m_processingState, *this, cmd);
}
void RadioState::handleGT(const QString &cmd) {
    ProcessingHandlers::handleGT(m_processingState, *this, cmd);
}
void RadioState::handleGTSub(const QString &cmd) {
    ProcessingHandlers::handleGTSub(m_processingState, *this, cmd);
}
void RadioState::handleNM(const QString &cmd) {
    ProcessingHandlers::handleNM(m_processingState, *this, cmd);
}
void RadioState::handleNMSub(const QString &cmd) {
    ProcessingHandlers::handleNMSub(m_processingState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Audio/Effects
// =============================================================================

void RadioState::handleFX(const QString &cmd) {
    AudioEffectsHandlers::handleFX(m_audioEffectsState, *this, cmd);
}

void RadioState::handleAP(const QString &cmd) {
    AudioEffectsHandlers::handleAP(m_audioEffectsState, *this, cmd);
}

void RadioState::handleAPSub(const QString &cmd) {
    AudioEffectsHandlers::handleAPSub(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - VFO Control
// =============================================================================

void RadioState::handleLN(const QString &cmd) {
    // LN - VFO Link
    if (cmd.length() < 3)
        return;
    bool ok;
    int ln = cmd.mid(2).toInt(&ok);
    if (ok) {
        bool linked = (ln == 1);
        if (linked != m_vfoLink) {
            m_vfoLink = linked;
            emit vfoLinkChanged(m_vfoLink);
        }
    }
}

// =============================================================================
// Individual Command Handlers - VOX
// =============================================================================

void RadioState::handleVX(const QString &cmd) {
    AudioEffectsHandlers::handleVX(m_audioEffectsState, *this, cmd);
}

void RadioState::handleVG(const QString &cmd) {
    AudioEffectsHandlers::handleVG(m_audioEffectsState, *this, cmd);
}

void RadioState::handleVI(const QString &cmd) {
    AudioEffectsHandlers::handleVI(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Audio I/O
// =============================================================================

void RadioState::handleLO(const QString &cmd) {
    AudioEffectsHandlers::handleLO(m_audioEffectsState, *this, cmd);
}

void RadioState::handleLI(const QString &cmd) {
    AudioEffectsHandlers::handleLI(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMI(const QString &cmd) {
    AudioEffectsHandlers::handleMI(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMS(const QString &cmd) {
    AudioEffectsHandlers::handleMS(m_audioEffectsState, *this, cmd);
}

void RadioState::handleES(const QString &cmd) {
    AudioEffectsHandlers::handleES(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - QSK/Delay
// =============================================================================

void RadioState::handleSD(const QString &cmd) {
    // SD - QSK/VOX Delay: SDxMzzz where x=QSK flag, M=mode, zzz=delay
    if (cmd.length() < 7)
        return;
    QChar qskFlag = cmd.at(2);
    QChar modeChar = cmd.at(3);
    bool ok;
    int delay = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;

    // Update QSK enabled state (only meaningful for CW mode)
    if (modeChar == 'C') {
        bool qskOn = (qskFlag == '1');
        if (qskOn != m_qskEnabled) {
            m_qskEnabled = qskOn;
            emit qskEnabledChanged(m_qskEnabled);
        }
    }

    bool isCurrentMode = false;
    const Mode current = mode();
    if (modeChar == 'C') {
        if (m_qskDelayCW != delay) {
            m_qskDelayCW = delay;
            isCurrentMode = (current == CW || current == CW_R);
        }
    } else if (modeChar == 'V') {
        if (m_qskDelayVoice != delay) {
            m_qskDelayVoice = delay;
            isCurrentMode = (current == LSB || current == USB || current == AM || current == FM);
        }
    } else if (modeChar == 'D') {
        if (m_qskDelayData != delay) {
            m_qskDelayData = delay;
            isCurrentMode = (current == DATA || current == DATA_R);
        }
    }
    if (isCurrentMode) {
        emit qskDelayChanged(delay);
    }
}

// =============================================================================
// Individual Command Handlers - Control State
// =============================================================================

void RadioState::handleSL(const QString &cmd) {
    DataControlHandlers::handleSL(m_dataControlState, *this, cmd);
}

void RadioState::handleSB(const QString &cmd) {
    // SB - Sub Receiver: SB0=off, SB1=on, SB3=on (diversity)
    if (cmd.length() <= 2)
        return;
    bool newState = (cmd.mid(2) != "0");
    if (newState != m_subReceiverEnabled) {
        m_subReceiverEnabled = newState;
        emit subRxEnabledChanged(m_subReceiverEnabled);
    }
}

void RadioState::handleDV(const QString &cmd) {
    // DV - Diversity
    if (cmd.length() <= 2)
        return;
    bool newState = (cmd.mid(2) == "1");
    if (newState != m_diversityEnabled) {
        m_diversityEnabled = newState;
        emit diversityChanged(m_diversityEnabled);
    }
}

void RadioState::handleTS(const QString &cmd) {
    // TS - Test Mode
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != m_testMode) {
        m_testMode = enabled;
        emit testModeChanged(m_testMode);
    }
}

void RadioState::handleBS(const QString &cmd) {
    // BS - B SET
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != m_bSetEnabled) {
        m_bSetEnabled = enabled;
        emit bSetChanged(m_bSetEnabled);
    }
}

// =============================================================================
// Individual Command Handlers - Antenna
// =============================================================================

// Antenna CAT handlers — delegate to AntennaHandlers namespace.
void RadioState::handleAN(const QString &cmd) {
    AntennaHandlers::handleAN(m_antennaState, *this, cmd);
}
void RadioState::handleAT(const QString &cmd) {
    AntennaHandlers::handleAT(m_antennaState, *this, cmd);
}
void RadioState::handleACN(const QString &cmd) {
    AntennaHandlers::handleACN(m_antennaState, *this, cmd);
}
void RadioState::handleACM(const QString &cmd) {
    AntennaHandlers::handleACM(m_antennaState, *this, cmd);
}
void RadioState::handleACS(const QString &cmd) {
    AntennaHandlers::handleACS(m_antennaState, *this, cmd);
}
void RadioState::handleACT(const QString &cmd) {
    AntennaHandlers::handleACT(m_antennaState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - RIT/XIT
// =============================================================================

// RIT / XIT CAT handlers — delegate to FrequencyVfoHandlers namespace.
void RadioState::handleRT(const QString &cmd) {
    FrequencyVfoHandlers::handleRT(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleXT(const QString &cmd) {
    FrequencyVfoHandlers::handleXT(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleRO(const QString &cmd) {
    FrequencyVfoHandlers::handleRO(m_frequencyVfoState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Text Decode
// =============================================================================

// Text decode CAT handlers — delegate to TextDecodeHandlers namespace.
void RadioState::handleTD(const QString &cmd) {
    TextDecodeHandlers::handleTD(m_textDecodeState, *this, cmd);
}
void RadioState::handleTDSub(const QString &cmd) {
    TextDecodeHandlers::handleTDSub(m_textDecodeState, *this, cmd);
}
void RadioState::handleTB(const QString &cmd) {
    TextDecodeHandlers::handleTB(*this, cmd);
}
void RadioState::handleTBSub(const QString &cmd) {
    TextDecodeHandlers::handleTBSub(*this, cmd);
}

// =============================================================================
// Individual Command Handlers - Data Mode
// =============================================================================

void RadioState::handleDT(const QString &cmd) {
    DataControlHandlers::handleDT(m_dataControlState, *this, cmd);
}
void RadioState::handleDTSub(const QString &cmd) {
    DataControlHandlers::handleDTSub(m_dataControlState, *this, cmd);
}
void RadioState::handleDR(const QString &cmd) {
    DataControlHandlers::handleDR(m_dataControlState, *this, cmd);
}
void RadioState::handleDRSub(const QString &cmd) {
    DataControlHandlers::handleDRSub(m_dataControlState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Equalizer
// =============================================================================

void RadioState::handleRE(const QString &cmd) {
    AudioEffectsHandlers::handleRE(m_audioEffectsState, *this, cmd);
}

void RadioState::handleTE(const QString &cmd) {
    AudioEffectsHandlers::handleTE(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Radio Info
// =============================================================================

void RadioState::handleID(const QString &cmd) {
    if (cmd.length() > 2)
        m_radioID = cmd.mid(2);
}

void RadioState::handleOM(const QString &cmd) {
    // OM format: 12-char string where each position indicates an option
    // Positions: 0=ATU, 1=PA, 2=XVTR, 3=SubRX, 4=HD, 5=Mini, 6=Linear, 7=KPA1500, 8=model marker
    if (cmd.length() <= 2)
        return;
    m_optionModules = cmd.mid(2).trimmed();
    if (m_optionModules.length() > 8) {
        bool hasS = m_optionModules.length() > 3 && m_optionModules[3] == 'S';
        bool hasH = m_optionModules.length() > 4 && m_optionModules[4] == 'H';
        bool has4 = m_optionModules.length() > 8 && m_optionModules[8] == '4';

        if (hasH && hasS && has4) {
            m_radioModel = "K4HD";
        } else if (hasS && has4) {
            m_radioModel = "K4D";
        } else if (has4) {
            m_radioModel = "K4";
        }
    }
}

void RadioState::handleRV(const QString &cmd) {
    // RV.COMPONENT-VERSION format (e.g., "RV.DDC0-00.65 (0:35)")
    if (cmd.length() <= 3)
        return;
    QString versionData = cmd.mid(3);
    int dashIndex = versionData.indexOf('-');
    if (dashIndex > 0) {
        QString component = versionData.left(dashIndex);
        QString version = versionData.mid(dashIndex + 1);
        m_firmwareVersions[component] = version;
    }
}

void RadioState::handleSIFP(const QString &cmd) {
    QString data = cmd.mid(4);
    // Parse VS (voltage)
    int vsIndex = data.indexOf("VS:");
    if (vsIndex >= 0) {
        int commaIndex = data.indexOf(',', vsIndex);
        QString vsStr =
            (commaIndex > vsIndex) ? data.mid(vsIndex + 3, commaIndex - vsIndex - 3) : data.mid(vsIndex + 3);
        bool ok;
        double voltage = vsStr.toDouble(&ok);
        if (ok && voltage != m_supplyVoltage) {
            m_supplyVoltage = voltage;
            emit supplyVoltageChanged(m_supplyVoltage);
        }
    }
    // Parse IS (current)
    int isIndex = data.indexOf("IS:");
    if (isIndex >= 0) {
        int commaIndex = data.indexOf(',', isIndex);
        QString isStr =
            (commaIndex > isIndex) ? data.mid(isIndex + 3, commaIndex - isIndex - 3) : data.mid(isIndex + 3);
        bool ok;
        double current = isStr.toDouble(&ok);
        if (ok && current != m_supplyCurrent) {
            m_supplyCurrent = current;
            emit supplyCurrentChanged(m_supplyCurrent);
        }
    }
}

void RadioState::handleMN(const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int bank = cmd.mid(2).toInt(&ok);
    if (ok && bank >= 1 && bank <= 4 && bank != m_messageBank) {
        m_messageBank = bank;
        emit messageBankChanged(m_messageBank);
    }
}

void RadioState::handleER(const QString &cmd) {
    int colonPos = cmd.indexOf(':');
    if (colonPos > 2) {
        bool ok;
        int errorCode = cmd.mid(2, colonPos - 2).toInt(&ok);
        if (ok)
            emit errorNotificationReceived(errorCode, cmd.mid(colonPos + 1));
    }
}

// =============================================================================
// Individual Command Handlers - Display (# prefix)
// =============================================================================

void RadioState::handleDisplaySCL(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int scale = cmd.mid(4).toInt(&ok);
    if (ok && scale >= 10 && scale <= 150 && scale != m_scale) {
        m_scale = scale;
        emit scaleChanged(m_scale);
    }
}

void RadioState::handleDisplayDPM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_dualPanModeLcd) {
        m_dualPanModeLcd = mode;
        emit dualPanModeLcdChanged(m_dualPanModeLcd);
    }
}

void RadioState::handleDisplayHDPM(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_dualPanModeExt) {
        m_dualPanModeExt = mode;
        emit dualPanModeExtChanged(m_dualPanModeExt);
    }
}

void RadioState::handleDisplayDSM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != m_displayModeLcd) {
        m_displayModeLcd = mode;
        emit displayModeLcdChanged(m_displayModeLcd);
    }
}

void RadioState::handleDisplayHDSM(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != m_displayModeExt) {
        m_displayModeExt = mode;
        emit displayModeExtChanged(m_displayModeExt);
    }
}

void RadioState::handleDisplayFPS(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fps = cmd.mid(4).toInt(&ok);
    if (ok && fps >= 12 && fps <= 30 && fps != m_displayFps) {
        m_displayFps = fps;
        emit displayFpsChanged(m_displayFps);
    }
}

void RadioState::handleDisplayWFC(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int color = cmd.mid(4).toInt(&ok);
    if (ok && color >= 0 && color <= 4 && color != m_waterfallColor) {
        m_waterfallColor = color;
        emit waterfallColorChanged(m_waterfallColor);
    }
}

void RadioState::handleDisplayWFH(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int height = cmd.mid(4).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != m_waterfallHeight) {
        m_waterfallHeight = height;
        emit waterfallHeightChanged(m_waterfallHeight);
    }
}

void RadioState::handleDisplayHWFH(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int height = cmd.mid(5).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != m_waterfallHeightExt) {
        m_waterfallHeightExt = height;
        emit waterfallHeightExtChanged(m_waterfallHeightExt);
    }
}

void RadioState::handleDisplayAVG(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int avg = cmd.mid(4).toInt(&ok);
    if (ok && avg >= 1 && avg <= 20 && avg != m_averaging) {
        m_averaging = avg;
        emit averagingChanged(m_averaging);
    }
}

void RadioState::handleDisplayPKM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int pkm = cmd.mid(4).toInt(&ok);
    if (ok && (pkm == 0 || pkm == 1) && pkm != m_peakMode) {
        m_peakMode = pkm;
        emit peakModeChanged(m_peakMode > 0);
    }
}

void RadioState::handleDisplayFXT(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxt = cmd.mid(4).toInt(&ok);
    if (ok && (fxt == 0 || fxt == 1) && fxt != m_fixedTune) {
        m_fixedTune = fxt;
        emit fixedTuneChanged(m_fixedTune, m_fixedTuneMode);
    }
}

void RadioState::handleDisplayFXA(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxa = cmd.mid(4).toInt(&ok);
    if (ok && fxa >= 0 && fxa <= 4 && fxa != m_fixedTuneMode) {
        m_fixedTuneMode = fxa;
        emit fixedTuneChanged(m_fixedTune, m_fixedTuneMode);
    }
}

void RadioState::handleDisplayFRZ(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int frz = cmd.mid(4).toInt(&ok);
    if (ok && (frz == 0 || frz == 1) && frz != m_freeze) {
        m_freeze = frz;
        emit freezeChanged(m_freeze > 0);
    }
}

void RadioState::handleDisplayVFA(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfa = cmd.mid(4).toInt(&ok);
    if (ok && vfa >= 0 && vfa <= 3 && vfa != m_vfoACursor) {
        m_vfoACursor = vfa;
        emit vfoACursorChanged(m_vfoACursor);
    }
}

void RadioState::handleDisplayVFB(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfb = cmd.mid(4).toInt(&ok);
    if (ok && vfb >= 0 && vfb <= 3 && vfb != m_vfoBCursor) {
        m_vfoBCursor = vfb;
        emit vfoBCursorChanged(m_vfoBCursor);
    }
}

void RadioState::handleDisplayAR(const QString &cmd) {
    if (cmd.length() < 12)
        return;
    QChar mode = cmd.at(cmd.length() - 1);
    int newValue = (mode == 'A') ? 1 : 0;
    if (newValue != m_autoRefLevel) {
        m_autoRefLevel = newValue;
        emit autoRefLevelChanged(m_autoRefLevel > 0);
    }
}

void RadioState::handleDisplayNB(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_ddcNbMode) {
        m_ddcNbMode = mode;
        emit ddcNbModeChanged(m_ddcNbMode);
    }
}

void RadioState::handleDisplayNBL(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int level = cmd.mid(5).toInt(&ok);
    if (ok && level >= 0 && level <= 14 && level != m_ddcNbLevel) {
        m_ddcNbLevel = level;
        emit ddcNbLevelChanged(m_ddcNbLevel);
    }
}
