// Regression tests for HalikeyEdge::acceptEdge — the same-direction dedupe helper
// used by HalikeyDevice for both MIDI and V1.4 paddle event streams.
//
// History: an earlier version of this helper also enforced a 3 ms processing-time gate
// (DEBOUNCE_NS) and dropped genuine MIDI releases on Windows when WinMM delivered
// press+release in a single burst, latching the paddle "on." It also failed to update
// `confirmed` when it rejected an edge, so any rejected transition could leave state
// stale indefinitely. Both flaws are now structurally impossible: the helper carries
// no time state and updates `confirmed` on every accepted transition.
//
// See docs/halikey-midi-windows-debounce-bug.md.

#include "hardware/halikey_edge.h"
#include <QObject>
#include <QtTest/QtTest>
#include <atomic>

class TestHalikeyDevice : public QObject {
    Q_OBJECT

private slots:
    void redundantPressDropped();
    void backToBackPressReleaseBothAccepted();
    void releaseAfterRedundantPressStillAccepted();
    void independentLinesDoNotCrosstalk();
};

void TestHalikeyDevice::redundantPressDropped() {
    std::atomic<bool> confirmed{false};

    QVERIFY(HalikeyEdge::acceptEdge(true, confirmed)); // initial press accepted
    QCOMPARE(confirmed.load(), true);
    QVERIFY(!HalikeyEdge::acceptEdge(true, confirmed)); // repeat press dropped
    QCOMPARE(confirmed.load(), true);                   // confirmed unchanged
}

void TestHalikeyDevice::backToBackPressReleaseBothAccepted() {
    // This is the Windows MIDI bug as a regression gate. Before the fix, two events <3 ms
    // apart in processing time would have the second dropped. Now there is no time gate.
    std::atomic<bool> confirmed{false};

    QVERIFY(HalikeyEdge::acceptEdge(true, confirmed));  // press
    QVERIFY(HalikeyEdge::acceptEdge(false, confirmed)); // release immediately after — must accept
    QCOMPARE(confirmed.load(), false);
}

void TestHalikeyDevice::releaseAfterRedundantPressStillAccepted() {
    // Guards the latent "rejected edge leaves confirmed stale" bug. Even when a duplicate
    // press is dropped, the subsequent release must still be recognized as a transition.
    std::atomic<bool> confirmed{false};

    QVERIFY(HalikeyEdge::acceptEdge(true, confirmed));  // press accepted
    QVERIFY(!HalikeyEdge::acceptEdge(true, confirmed)); // duplicate dropped
    QVERIFY(HalikeyEdge::acceptEdge(false, confirmed)); // release still accepted
    QCOMPARE(confirmed.load(), false);
}

void TestHalikeyDevice::independentLinesDoNotCrosstalk() {
    // Three separate atomics (as HalikeyDevice uses for dit / dah / ptt) must remain
    // independent — pressing one line never confirms another.
    std::atomic<bool> dit{false};
    std::atomic<bool> dah{false};
    std::atomic<bool> ptt{false};

    QVERIFY(HalikeyEdge::acceptEdge(true, dit));
    QCOMPARE(dit.load(), true);
    QCOMPARE(dah.load(), false);
    QCOMPARE(ptt.load(), false);

    QVERIFY(HalikeyEdge::acceptEdge(true, ptt));
    QCOMPARE(dit.load(), true);
    QCOMPARE(dah.load(), false);
    QCOMPARE(ptt.load(), true);

    QVERIFY(HalikeyEdge::acceptEdge(false, dit));
    QCOMPARE(dit.load(), false);
    QCOMPARE(dah.load(), false);
    QCOMPARE(ptt.load(), true);
}

QTEST_GUILESS_MAIN(TestHalikeyDevice)
#include "test_halikeydevice.moc"
