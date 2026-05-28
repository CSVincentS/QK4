#include <QTest>
#include "utils/radioutils.h"

class TestRadioUtils : public QObject {
    Q_OBJECT

private slots:
    // tuningStepToHz
    void testTuningStep_0() { QCOMPARE(RadioUtils::tuningStepToHz(0), 1); }
    void testTuningStep_1() { QCOMPARE(RadioUtils::tuningStepToHz(1), 10); }
    void testTuningStep_2() { QCOMPARE(RadioUtils::tuningStepToHz(2), 100); }
    void testTuningStep_3() { QCOMPARE(RadioUtils::tuningStepToHz(3), 1000); }
    void testTuningStep_4() { QCOMPARE(RadioUtils::tuningStepToHz(4), 10000); }
    void testTuningStep_5() { QCOMPARE(RadioUtils::tuningStepToHz(5), 100); }
    void testTuningStep_outOfRange_negative() { QCOMPARE(RadioUtils::tuningStepToHz(-1), 1000); }
    void testTuningStep_outOfRange_high() { QCOMPARE(RadioUtils::tuningStepToHz(6), 1000); }

    // getBandFromFrequency
    void testBand_160m() { QCOMPARE(RadioUtils::getBandFromFrequency(1900000), 0); }
    void testBand_80m() { QCOMPARE(RadioUtils::getBandFromFrequency(3573000), 1); }
    void testBand_60m() { QCOMPARE(RadioUtils::getBandFromFrequency(5357000), 2); }
    void testBand_40m() { QCOMPARE(RadioUtils::getBandFromFrequency(7074000), 3); }
    void testBand_30m() { QCOMPARE(RadioUtils::getBandFromFrequency(10136000), 4); }
    void testBand_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14074000), 5); }
    void testBand_17m() { QCOMPARE(RadioUtils::getBandFromFrequency(18100000), 6); }
    void testBand_15m() { QCOMPARE(RadioUtils::getBandFromFrequency(21074000), 7); }
    void testBand_12m() { QCOMPARE(RadioUtils::getBandFromFrequency(24915000), 8); }
    void testBand_10m() { QCOMPARE(RadioUtils::getBandFromFrequency(28074000), 9); }
    void testBand_6m() { QCOMPARE(RadioUtils::getBandFromFrequency(50313000), 10); }
    void testBand_xvtr() { QCOMPARE(RadioUtils::getBandFromFrequency(144174000), 16); }
    void testBand_outOfBand() { QCOMPARE(RadioUtils::getBandFromFrequency(100000), -1); }
    void testBand_gapBetweenBands() { QCOMPARE(RadioUtils::getBandFromFrequency(8000000), -1); }
    void testBand_lowerEdge_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14000000), 5); }
    void testBand_upperEdge_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14350000), 5); }
    void testBand_justAbove_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14350001), -1); }

    // getNextSpanUp
    void testSpanUp_min() { QCOMPARE(RadioUtils::getNextSpanUp(5000), 6000); }
    void testSpanUp_belowThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(100000), 101000); }
    void testSpanUp_atThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(144000), 148000); }
    void testSpanUp_aboveThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(200000), 204000); }
    void testSpanUp_atMax() { QCOMPARE(RadioUtils::getNextSpanUp(368000), 368000); }
    void testSpanUp_nearMax() { QCOMPARE(RadioUtils::getNextSpanUp(366000), 368000); }

    // getNextSpanDown
    void testSpanDown_atMin() { QCOMPARE(RadioUtils::getNextSpanDown(5000), 5000); }
    void testSpanDown_nearMin() { QCOMPARE(RadioUtils::getNextSpanDown(6000), 5000); }
    void testSpanDown_belowThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(100000), 99000); }
    void testSpanDown_aboveThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(200000), 196000); }
    void testSpanDown_atThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(140000), 139000); }
    void testSpanDown_justAboveThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(141000), 137000); }

    // buildEqCommand
    void testBuildEqCommand_flat() {
        QVector<int> flat(8, 0);
        QCOMPARE(RadioUtils::buildEqCommand("RE", flat), QString("RE+00+00+00+00+00+00+00+00"));
    }

    void testBuildEqCommand_mixed() {
        QVector<int> bands = {-16, -5, 0, 3, 8, 12, -1, 16};
        QCOMPARE(RadioUtils::buildEqCommand("TE", bands), QString("TE-16-05+00+03+08+12-01+16"));
    }

    void testBuildEqCommand_txPrefix() {
        QVector<int> bands(8, 5);
        QCOMPARE(RadioUtils::buildEqCommand("TE", bands), QString("TE+05+05+05+05+05+05+05+05"));
    }

    // Constants
    void testConstants() {
        QCOMPARE(RadioUtils::SPAN_MIN, 5000);
        QCOMPARE(RadioUtils::SPAN_MAX, 368000);
        QVERIFY(RadioUtils::SPAN_THRESHOLD_UP > RadioUtils::SPAN_THRESHOLD_DOWN);
    }

    // slTierToFrameSamples — verified via pcap at all SL tiers
    void testSlTier_SL0() { QCOMPARE(RadioUtils::slTierToFrameSamples(0), 240); }
    void testSlTier_SL1() { QCOMPARE(RadioUtils::slTierToFrameSamples(1), 480); }
    void testSlTier_SL2() { QCOMPARE(RadioUtils::slTierToFrameSamples(2), 480); }
    void testSlTier_SL3() { QCOMPARE(RadioUtils::slTierToFrameSamples(3), 720); }
    void testSlTier_SL4() { QCOMPARE(RadioUtils::slTierToFrameSamples(4), 720); }
    void testSlTier_SL5() { QCOMPARE(RadioUtils::slTierToFrameSamples(5), 720); }
    void testSlTier_SL6() { QCOMPARE(RadioUtils::slTierToFrameSamples(6), 1440); }
    void testSlTier_SL7() { QCOMPARE(RadioUtils::slTierToFrameSamples(7), 1440); }
    void testSlTier_outOfRange_negative() { QCOMPARE(RadioUtils::slTierToFrameSamples(-1), 720); }
    void testSlTier_outOfRange_high() { QCOMPARE(RadioUtils::slTierToFrameSamples(8), 720); }

    // fixedTuneModeFromCat — verified against hardware + K4 command reference 2026-05-28
    // FXT0 = Track (FXA ignored); FXT1: 0=Fixed1,1=Fixed2,2=Slide1,3=Static,4=Slide2
    void testFixedTune_track_fxt0() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(0, 0) == RadioUtils::FixedTuneMode::Track);
        QVERIFY(RadioUtils::fixedTuneModeFromCat(0, 4) == RadioUtils::FixedTuneMode::Track); // FXA ignored
    }
    void testFixedTune_fixed1_fxa0() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 0) == RadioUtils::FixedTuneMode::Fixed1);
    }
    void testFixedTune_fixed2_fxa1() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 1) == RadioUtils::FixedTuneMode::Fixed2);
    }
    void testFixedTune_slide1_fxa2() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 2) == RadioUtils::FixedTuneMode::Slide1);
    }
    void testFixedTune_static_fxa3() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 3) == RadioUtils::FixedTuneMode::Static);
    }
    void testFixedTune_slide2_fxa4() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 4) == RadioUtils::FixedTuneMode::Slide2);
    }
    void testFixedTune_unknownFxa_fallsBackToTrack() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 9) == RadioUtils::FixedTuneMode::Track);
    }

    // fixedTuneSetCommand — exact SET strings (FXA before FXT; Track sends only FXT0)
    void testFixedTuneCmd_track() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Track), QString("#FXT0;"));
    }
    void testFixedTuneCmd_fixed1() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Fixed1), QString("#FXA0;#FXT1;"));
    }
    void testFixedTuneCmd_fixed2() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Fixed2), QString("#FXA1;#FXT1;"));
    }
    void testFixedTuneCmd_slide1() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Slide1), QString("#FXA2;#FXT1;"));
    }
    void testFixedTuneCmd_static() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Static), QString("#FXA3;#FXT1;"));
    }
    void testFixedTuneCmd_slide2() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Slide2), QString("#FXA4;#FXT1;"));
    }

    // Round-trip: every fixed mode's SET command re-decodes to the same mode.
    void testFixedTune_roundTrip() {
        const RadioUtils::FixedTuneMode modes[] = {RadioUtils::FixedTuneMode::Fixed1, RadioUtils::FixedTuneMode::Fixed2,
                                                   RadioUtils::FixedTuneMode::Slide1, RadioUtils::FixedTuneMode::Static,
                                                   RadioUtils::FixedTuneMode::Slide2};
        for (auto mode : modes) {
            QString cmd = RadioUtils::fixedTuneSetCommand(mode); // e.g. "#FXA2;#FXT1;"
            int fxa = cmd.mid(4, 1).toInt();                     // digit after "#FXA"
            QVERIFY(RadioUtils::fixedTuneModeFromCat(1, fxa) == mode);
        }
    }
};

QTEST_MAIN(TestRadioUtils)
#include "test_radioutils.moc"
