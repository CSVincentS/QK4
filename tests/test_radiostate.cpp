#include <QTest>
#include <QSignalSpy>
#include "models/radiostate.h"

class TestRadioState : public QObject {
    Q_OBJECT

private slots:
    // Frequency parsing
    void testFrequencyA() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        QCOMPARE(rs.frequency(), quint64(14074000));
        QCOMPARE(rs.vfoA(), quint64(14074000));
    }

    void testFrequencyB() {
        RadioState rs;
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(rs.vfoB(), quint64(7074000));
    }

    void testFrequencySignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FA00014074000;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(14074000));
    }

    void testFrequencyNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FA00014074000;"); // same value
        QCOMPARE(spy.count(), 0);             // no signal for unchanged value
    }

    // Mode parsing
    void testModeA_LSB() {
        RadioState rs;
        rs.parseCATCommand("MD1;");
        QCOMPARE(rs.mode(), RadioState::LSB);
    }

    void testModeA_USB() {
        RadioState rs;
        rs.parseCATCommand("MD2;");
        QCOMPARE(rs.mode(), RadioState::USB);
    }

    void testModeA_CW() {
        RadioState rs;
        rs.parseCATCommand("MD3;");
        QCOMPARE(rs.mode(), RadioState::CW);
    }

    void testModeA_DATA() {
        RadioState rs;
        rs.parseCATCommand("MD6;");
        QCOMPARE(rs.mode(), RadioState::DATA);
    }

    void testModeB() {
        RadioState rs;
        rs.parseCATCommand("MD$2;");
        QCOMPARE(rs.modeB(), RadioState::USB);
    }

    // Power parsing (PCnnnr format)
    void testPowerQRO() {
        RadioState rs;
        rs.parseCATCommand("PC050H;");
        QCOMPARE(rs.rfPower(), 50.0);
        QCOMPARE(rs.isQrpMode(), false);
    }

    void testPowerQRP() {
        RadioState rs;
        rs.parseCATCommand("PC099L;");
        QCOMPARE(rs.rfPower(), 9.9);
        QCOMPARE(rs.isQrpMode(), true);
    }

    void testPowerQRP_OneWatt() {
        RadioState rs;
        rs.parseCATCommand("PC010L;");
        QCOMPARE(rs.rfPower(), 1.0);
        QCOMPARE(rs.isQrpMode(), true);
    }

    // Filter position (FP/FP$ — uses handleIntPair helper)
    void testFilterPositionA() {
        RadioState rs;
        rs.parseCATCommand("FP1;");
        QCOMPARE(rs.filterPosition(), 1);
    }

    void testFilterPositionB() {
        RadioState rs;
        rs.parseCATCommand("FP$2;");
        QCOMPARE(rs.filterPositionB(), 2);
    }

    void testFilterPositionOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("FP1;");       // valid
        rs.parseCATCommand("FP9;");       // out of range (1-3)
        QCOMPARE(rs.filterPosition(), 1); // unchanged
    }

    // Bandwidth (BW/BW$ — ×10 multiplier)
    void testBandwidthA() {
        RadioState rs;
        rs.parseCATCommand("BW240;");
        QCOMPARE(rs.filterBandwidth(), 2400);
    }

    void testBandwidthB() {
        RadioState rs;
        rs.parseCATCommand("BW$300;");
        QCOMPARE(rs.filterBandwidthB(), 3000);
    }

    // IF Shift (IS/IS$)
    void testIfShiftA() {
        RadioState rs;
        rs.parseCATCommand("IS50;");
        QCOMPARE(rs.ifShift(), 50);
    }

    void testIfShiftB() {
        RadioState rs;
        rs.parseCATCommand("IS$45;");
        QCOMPARE(rs.ifShiftB(), 45);
    }

    // Auto notch (NA/NA$ — uses handleBoolPair helper)
    void testAutoNotchA_On() {
        RadioState rs;
        rs.parseCATCommand("NA1;");
        QCOMPARE(rs.autoNotchEnabled(), true);
    }

    void testAutoNotchA_Off() {
        RadioState rs;
        rs.parseCATCommand("NA1;");
        rs.parseCATCommand("NA0;");
        QCOMPARE(rs.autoNotchEnabled(), false);
    }

    void testAutoNotchB() {
        RadioState rs;
        rs.parseCATCommand("NA$1;");
        QCOMPARE(rs.autoNotchEnabledB(), true);
    }

    // VFO Lock (LK/LK$ — uses handleBoolPairVal helper)
    void testLockA() {
        RadioState rs;
        rs.parseCATCommand("LK1;");
        QCOMPARE(rs.lockA(), true);
    }

    void testLockB() {
        RadioState rs;
        rs.parseCATCommand("LK$1;");
        QCOMPARE(rs.lockB(), true);
    }

    // Split
    void testSplitOn() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        QCOMPARE(rs.splitEnabled(), true);
    }

    void testSplitOff() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        rs.parseCATCommand("FT0;");
        QCOMPARE(rs.splitEnabled(), false);
    }

    // Malformed/edge cases — must not crash
    void testEmptyCommand() {
        RadioState rs;
        rs.parseCATCommand("");               // empty
        rs.parseCATCommand(";");              // just semicolon
        QCOMPARE(rs.frequency(), quint64(0)); // unchanged from default
    }

    void testShortCommand() {
        RadioState rs;
        rs.parseCATCommand("FA;");            // too short for frequency
        QCOMPARE(rs.frequency(), quint64(0)); // unchanged
    }

    void testUnknownCommand() {
        RadioState rs;
        rs.parseCATCommand("ZZ999;");         // unknown prefix
        QCOMPARE(rs.frequency(), quint64(0)); // no crash, no change
    }

    void testPowerTooShort() {
        RadioState rs;
        rs.parseCATCommand("PC;");    // no data
        rs.parseCATCommand("PC00;");  // too short for PCnnnr (need 6)
        QCOMPARE(rs.rfPower(), -1.0); // unchanged from default sentinel
    }

    // CW Pitch
    void testCwPitch() {
        RadioState rs;
        // CW pitch format: CWnn; where nn = pitch/10 (range 25-95 → 250-950 Hz)
        rs.parseCATCommand("CW60;");
        QCOMPARE(rs.cwPitch(), 600);
    }

    // Keyer Speed
    void testKeyerSpeed() {
        RadioState rs;
        rs.parseCATCommand("KS020;");
        QCOMPARE(rs.keyerSpeed(), 20);
    }

    // Streaming Latency (SL command)
    void testStreamingLatency() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::streamingLatencyChanged);
        rs.parseCATCommand("SL3;");
        QCOMPARE(rs.streamingLatency(), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
    }

    void testStreamingLatency_allValues() {
        RadioState rs;
        for (int i = 0; i <= 7; i++) {
            rs.parseCATCommand(QString("SL%1;").arg(i));
            QCOMPARE(rs.streamingLatency(), i);
        }
    }

    void testStreamingLatency_noSignalOnSameValue() {
        RadioState rs;
        rs.parseCATCommand("SL3;");
        QSignalSpy spy(&rs, &RadioState::streamingLatencyChanged);
        rs.parseCATCommand("SL3;");
        QCOMPARE(spy.count(), 0);
    }

    void testStreamingLatency_resetToSentinel() {
        RadioState rs;
        rs.parseCATCommand("SL5;");
        rs.reset();
        QCOMPARE(rs.streamingLatency(), -1);
    }

    // SIRC command removed — must not crash when received
    void testSircCommandIgnored() {
        RadioState rs;
        rs.parseCATCommand("SIRC1;");
        // No handler registered — command is silently ignored, no crash
    }

    // Reset clears all state
    void testReset() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        rs.parseCATCommand("MD2;");
        rs.parseCATCommand("FP1;");
        rs.reset();
        QCOMPARE(rs.frequency(), quint64(0));
        QCOMPARE(rs.mode(), RadioState::Unknown);
        QCOMPARE(rs.filterPosition(), -1); // sentinel
    }

    // =========================================================================
    // Phase 0.1 Backfill — FrequencyVfo subsystem
    // Handlers covered: FA, FB, FT, RT, RT$, XT, RO, RO$
    // =========================================================================

    // --- FB signal / idempotence (A/B symmetry with testFrequencySignalEmitted) ---
    void testFrequencyBSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(7074000));
    }

    void testFrequencyBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FB00007074000;");
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(spy.count(), 0);
    }

    // --- FA/FB non-numeric payload (toULongLong fails) ---
    void testFrequencyANonNumeric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FAABCDEFGHIJK;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.frequency(), quint64(0));
    }

    void testFrequencyBNonNumeric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FBABCDEFGHIJK;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.vfoB(), quint64(0));
    }

    // --- FA/FB low-edge values ---
    void testFrequencyALowEdge() {
        RadioState rs;
        rs.parseCATCommand("FA00000001000;"); // 1 kHz
        QCOMPARE(rs.frequency(), quint64(1000));
    }

    void testFrequencyBLowEdge() {
        RadioState rs;
        rs.parseCATCommand("FB00000001000;");
        QCOMPARE(rs.vfoB(), quint64(1000));
    }

    // --- Split (FT) signal + idempotence ---
    void testSplitSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::splitChanged);
        rs.parseCATCommand("FT1;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void testSplitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        QSignalSpy spy(&rs, &RadioState::splitChanged);
        rs.parseCATCommand("FT1;");
        QCOMPARE(spy.count(), 0);
    }

    void testSplitTreatsNonOneAsOff() {
        // handleFT uses strict "mid(2) == \"1\"" — anything else is "off"
        RadioState rs;
        rs.parseCATCommand("FT1;");
        rs.parseCATCommand("FT2;"); // not "1" → off
        QCOMPARE(rs.splitEnabled(), false);
    }

    // --- RIT on/off (RT) ---
    void testRitEnable() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QCOMPARE(rs.ritEnabled(), true);
    }

    void testRitDisable() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        rs.parseCATCommand("RT0;");
        QCOMPARE(rs.ritEnabled(), false);
    }

    void testRitIgnoresInvalidChar() {
        // handleRT returns silently if char at [2] is not '0' or '1'
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT2;"); // invalid
        rs.parseCATCommand("RTX;"); // invalid
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.ritEnabled(), true); // unchanged
    }

    void testRitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- XIT on/off (XT) ---
    void testXitEnable() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QCOMPARE(rs.xitEnabled(), true);
    }

    void testXitDisable() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        rs.parseCATCommand("XT0;");
        QCOMPARE(rs.xitEnabled(), false);
    }

    void testXitIgnoresInvalidChar() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("XT2;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.xitEnabled(), true);
    }

    void testXitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("XT1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- RIT/XIT offset (RO) ---
    void testRitXitOffsetPositive() {
        RadioState rs;
        rs.parseCATCommand("RO+0500;");
        QCOMPARE(rs.ritXitOffset(), 500);
    }

    void testRitXitOffsetNegative() {
        RadioState rs;
        rs.parseCATCommand("RO-0500;");
        QCOMPARE(rs.ritXitOffset(), -500);
    }

    void testRitXitOffsetZero() {
        // First set non-zero so we can detect the change to zero
        RadioState rs;
        rs.parseCATCommand("RO+0100;");
        rs.parseCATCommand("RO+0000;");
        QCOMPARE(rs.ritXitOffset(), 0);
    }

    void testRitXitOffsetNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RO+0500;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RO+0500;");
        QCOMPARE(spy.count(), 0);
    }

    // --- RT$/RO$ (VFO B RIT register) ---
    void testRitBEnable() {
        RadioState rs;
        rs.parseCATCommand("RT$1;");
        QCOMPARE(rs.ritEnabledB(), true);
    }

    void testRitBDisable() {
        RadioState rs;
        rs.parseCATCommand("RT$1;");
        rs.parseCATCommand("RT$0;");
        QCOMPARE(rs.ritEnabledB(), false);
    }

    void testRitXitBOffset() {
        RadioState rs;
        rs.parseCATCommand("RO$+0250;");
        QCOMPARE(rs.ritXitOffsetB(), 250);
    }

    void testRitXitBOffsetNegative() {
        RadioState rs;
        rs.parseCATCommand("RO$-0250;");
        QCOMPARE(rs.ritXitOffsetB(), -250);
    }

    // --- Rollup signal semantic: ritXitChanged carries rit AND xit AND offset ---
    // WHY: The RT/XT/RO handlers all fire the same rollup signal. A regression
    // that splits these onto separate signals (during Phase 1) would only
    // emit one arg — this test catches that.
    void testRitXitRollupCarriesAllThree() {
        RadioState rs;
        rs.parseCATCommand("XT1;");     // set XIT first
        rs.parseCATCommand("RO+0500;"); // set offset
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT1;"); // now enable RIT — should emit rollup
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true); // ritEnabled
        QCOMPARE(spy.at(0).at(1).toBool(), true); // xitEnabled still true
        QCOMPARE(spy.at(0).at(2).toInt(), 500);   // offset still 500
    }

    // --- A/B symmetry gap flagged by plan exploration ---
    // Paired with testFilterPositionOutOfRange.
    void testFilterPositionBOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("FP$2;");       // valid
        rs.parseCATCommand("FP$9;");       // out of range (1-3)
        QCOMPARE(rs.filterPositionB(), 2); // unchanged
    }

    // =========================================================================
    // Phase 0.1 Backfill — ProcessingState subsystem
    // Handlers: NB/NB$, NR/NR$, PA/PA$, RA/RA$, GT/GT$, NM/NM$, AP/AP$
    // All of NB/NR/PA/RA/GT fire the rollup signal processingChanged() /
    // processingChangedB(). Tests assert idempotent emission (same input →
    // no duplicate rollup).
    // =========================================================================

    // --- NB / NB$ (Noise Blanker: NBnnmf; nn=level 0-15, m=on/off, f=filter 0-2) ---
    void testNbParsesLevelEnabledFilter() {
        RadioState rs;
        rs.parseCATCommand("NB0512;"); // level=5, enabled=1, filter=2
        QCOMPARE(rs.noiseBlankerLevel(), 5);
        QCOMPARE(rs.noiseBlankerEnabled(), true);
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    void testNbLevelClampedTo15() {
        RadioState rs;
        rs.parseCATCommand("NB9912;");
        QCOMPARE(rs.noiseBlankerLevel(), 15);
    }

    void testNbFilterClampedTo2() {
        RadioState rs;
        rs.parseCATCommand("NB0519;"); // filter=9 → clamped to 2
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    void testNbDisable() {
        RadioState rs;
        rs.parseCATCommand("NB0511;");
        rs.parseCATCommand("NB0501;");
        QCOMPARE(rs.noiseBlankerEnabled(), false);
    }

    void testNbSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB0512;");
        QCOMPARE(spy.count(), 1);
    }

    void testNbNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NB0512;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB0512;");
        QCOMPARE(spy.count(), 0);
    }

    void testNbBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NB$0512;");
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NB$0512;");
        QCOMPARE(spy.count(), 0);
    }

    void testNbBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NB$0712;");
        QCOMPARE(rs.noiseBlankerLevelB(), 7);
        QCOMPARE(rs.noiseBlankerEnabledB(), true);
        QCOMPARE(rs.noiseBlankerFilterWidthB(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testNbShortCommandIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB;");   // way too short
        rs.parseCATCommand("NB01;"); // missing enable/filter
        QCOMPARE(spy.count(), 0);
    }

    void testNbPartialWithoutFilterPreservesCurrent() {
        // NB with only 3 chars after prefix (NBnnm) leaves filter untouched
        RadioState rs;
        rs.parseCATCommand("NB0512;"); // filter set to 2
        rs.parseCATCommand("NB071;");  // only level+enabled (3 chars) — filter stays 2
        QCOMPARE(rs.noiseBlankerLevel(), 7);
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    // --- NR / NR$ (Noise Reduction: NRnnm; nn=level, m=on/off) ---
    void testNrParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        QCOMPARE(rs.noiseReductionLevel(), 5);
        QCOMPARE(rs.noiseReductionEnabled(), true);
    }

    void testNrDisable() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        rs.parseCATCommand("NR050;");
        QCOMPARE(rs.noiseReductionEnabled(), false);
    }

    void testNrSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NR051;");
        QCOMPARE(spy.count(), 1);
    }

    void testNrNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NR051;");
        QCOMPARE(spy.count(), 0);
    }

    void testNrBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NR$071;");
        QCOMPARE(rs.noiseReductionLevelB(), 7);
        QCOMPARE(rs.noiseReductionEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testNrBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NR$071;");
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NR$071;");
        QCOMPARE(spy.count(), 0);
    }

    // --- PA / PA$ (Preamp: PAnm; n=level, m=on/off) ---
    void testPaParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        QCOMPARE(rs.preamp(), 2);
        QCOMPARE(rs.preampEnabled(), true);
    }

    void testPaDisable() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        rs.parseCATCommand("PA20;");
        QCOMPARE(rs.preampEnabled(), false);
    }

    void testPaSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("PA21;");
        QCOMPARE(spy.count(), 1);
    }

    void testPaNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("PA21;");
        QCOMPARE(spy.count(), 0);
    }

    void testPaBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("PA$11;");
        QCOMPARE(rs.preampB(), 1);
        QCOMPARE(rs.preampEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- RA / RA$ (Attenuator: RAnnm; nn=level, m=on/off) ---
    void testRaParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("RA051;");
        QCOMPARE(rs.attenuatorLevel(), 5);
        QCOMPARE(rs.attenuatorEnabled(), true);
    }

    void testRaSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("RA051;");
        QCOMPARE(spy.count(), 1);
    }

    void testRaNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RA051;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("RA051;");
        QCOMPARE(spy.count(), 0);
    }

    void testRaBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("RA$101;");
        QCOMPARE(rs.attenuatorLevelB(), 10);
        QCOMPARE(rs.attenuatorEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- GT / GT$ (AGC Speed: GTn; 0=Off, 1=Slow, 2=Fast) ---
    void testGtOff() {
        RadioState rs;
        rs.parseCATCommand("GT0;");
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Off);
    }

    void testGtSlow() {
        // WHY: default m_agcSpeed is AGC_Slow; setting it to AGC_Slow emits
        // nothing. Change state first so we observe the transition.
        RadioState rs;
        rs.parseCATCommand("GT0;"); // now Off
        rs.parseCATCommand("GT1;"); // → Slow
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Slow);
    }

    void testGtFast() {
        RadioState rs;
        rs.parseCATCommand("GT2;");
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Fast);
    }

    void testGtInvalidValueIgnored() {
        // Values outside 0-2 are silently ignored.
        RadioState rs;
        rs.parseCATCommand("GT2;"); // Fast
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT5;"); // invalid → no change
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Fast);
    }

    void testGtSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT2;"); // Slow (default) → Fast
        QCOMPARE(spy.count(), 1);
    }

    void testGtNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("GT2;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT2;");
        QCOMPARE(spy.count(), 0);
    }

    void testGtBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("GT$0;");
        QCOMPARE(rs.agcSpeedB(), RadioState::AGC_Off);
        QCOMPARE(spy.count(), 1);
    }

    // --- NM / NM$ (Manual Notch: NMnnnnm long form, or NMm short enable-only) ---
    void testNmParsesPitchAndEnabled() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // pitch=1000, enabled=1
        QCOMPARE(rs.manualNotchPitch(), 1000);
        QCOMPARE(rs.manualNotchEnabled(), true);
    }

    void testNmShortFormTogglesEnableOnly() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // establish pitch=1000
        rs.parseCATCommand("NM0;");     // enable only → disable
        QCOMPARE(rs.manualNotchEnabled(), false);
        QCOMPARE(rs.manualNotchPitch(), 1000); // pitch preserved
    }

    void testNmPitchBelowRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // pitch=1000
        rs.parseCATCommand("NM01001;"); // pitch=100 (< 150) — rejected
        QCOMPARE(rs.manualNotchPitch(), 1000);
    }

    void testNmPitchAboveRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("NM10001;");
        rs.parseCATCommand("NM60001;"); // pitch=6000 (> 5000) — rejected
        QCOMPARE(rs.manualNotchPitch(), 1000);
    }

    void testNmSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::notchChanged);
        rs.parseCATCommand("NM10001;");
        QCOMPARE(spy.count(), 1);
    }

    void testNmNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NM10001;");
        QSignalSpy spy(&rs, &RadioState::notchChanged);
        rs.parseCATCommand("NM10001;");
        QCOMPARE(spy.count(), 0);
    }

    void testNmBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::notchBChanged);
        rs.parseCATCommand("NM$10001;");
        QCOMPARE(rs.manualNotchPitchB(), 1000);
        QCOMPARE(rs.manualNotchEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- AP / AP$ (Audio Peak Filter: APmb; m=on/off, b=bandwidth 0-2) ---
    void testApParsesEnabledBandwidth() {
        RadioState rs;
        rs.parseCATCommand("AP11;");
        QCOMPARE(rs.apfEnabled(), true);
        QCOMPARE(rs.apfBandwidth(), 1);
    }

    void testApBandwidthClampedTo2() {
        RadioState rs;
        rs.parseCATCommand("AP19;"); // bandwidth=9 → clamped to 2
        QCOMPARE(rs.apfBandwidth(), 2);
    }

    void testApSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::apfChanged);
        rs.parseCATCommand("AP11;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true); // enabled
        QCOMPARE(spy.at(0).at(1).toInt(), 1);     // bandwidth
    }

    void testApNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AP11;");
        QSignalSpy spy(&rs, &RadioState::apfChanged);
        rs.parseCATCommand("AP11;");
        QCOMPARE(spy.count(), 0);
    }

    void testApBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::apfBChanged);
        rs.parseCATCommand("AP$11;");
        QCOMPARE(rs.apfEnabledB(), true);
        QCOMPARE(rs.apfBandwidthB(), 1);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestRadioState)
#include "test_radiostate.moc"
