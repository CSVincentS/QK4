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
};

QTEST_MAIN(TestRadioState)
#include "test_radiostate.moc"
