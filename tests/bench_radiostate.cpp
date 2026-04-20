// Phase 0.9 — RadioState::parseCATCommand() microbenchmark.
//
// Pushes a pre-canned mix of CAT commands through the dispatch registry,
// measures wall-clock time, and reports ns/op. The goal isn't absolute
// perf — it's a tripwire: if a Phase 1 refactor doubles dispatch time,
// the next bench run will make it visible.
//
// Representative mix: frequency, mode, bandwidth, multi-field processing,
// 3-arg antenna config, meter telemetry. Covers fast-path lambdas,
// handleIntPair helpers, and the complex multi-field handlers.
//
// Reports:
//   - total wall time for N iterations
//   - ns/op
// Does not fail or compare against a recorded baseline — CI just records
// the number and a human eyeballs it between commits. Treat >10%
// regression vs a known-good commit as a warning to investigate.

#include "models/radiostate.h"
#include <QElapsedTimer>
#include <QStringList>
#include <QtTest>
#include <cstdio>

class BenchRadioState : public QObject {
    Q_OBJECT

private slots:
    void benchDispatch() {
        RadioState rs;

        // Fixed mix representing realistic CAT traffic shapes:
        //   - Frequency (quint64 parse)
        //   - Mode (enum lookup)
        //   - Bandwidth (int × 10)
        //   - S-meter (streaming telemetry, always emits)
        //   - Filter position (handleIntPair)
        //   - NB (multi-field)
        //   - RIT offset (signed int)
        //   - Antenna (bounded int + rollup emit)
        //   - Scale (bounded int)
        //   - Unknown prefix (dispatch miss)
        const QStringList commandMix{
            QStringLiteral("FA00014074000;"), QStringLiteral("MD2;"), QStringLiteral("BW240;"),
            QStringLiteral("SM09;"),          QStringLiteral("FP1;"), QStringLiteral("NB0512;"),
            QStringLiteral("RO+0500;"),       QStringLiteral("AR5;"), QStringLiteral("#SCL080;"),
            QStringLiteral("ZZ999;"),
        };

        // Two bursts: the first shakes out branch prediction + caches, the
        // second is the one we report. Saves us from measuring registry
        // construction as dispatch cost.
        constexpr int kWarmupIters = 10'000;
        constexpr int kMeasuredIters = 1'000'000;

        for (int i = 0; i < kWarmupIters; ++i) {
            rs.parseCATCommand(commandMix.at(i % commandMix.size()));
        }

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < kMeasuredIters; ++i) {
            rs.parseCATCommand(commandMix.at(i % commandMix.size()));
        }
        const qint64 nsTotal = timer.nsecsElapsed();
        const double nsPerOp = static_cast<double>(nsTotal) / kMeasuredIters;

        std::printf("[bench_radiostate] %d dispatches in %.3f ms (%.1f ns/op)\n", kMeasuredIters, nsTotal / 1e6,
                    nsPerOp);

        // Reasonable upper bound. Current hardware pushes well below this;
        // anything higher is a real regression to investigate. Not a hard
        // CI gate — this is a tripwire, not a fence.
        QVERIFY2(nsPerOp < 10'000, qPrintable(QStringLiteral("Dispatch slower than 10us/op: %1 ns/op").arg(nsPerOp)));
    }
};

QTEST_MAIN(BenchRadioState)
#include "bench_radiostate.moc"
