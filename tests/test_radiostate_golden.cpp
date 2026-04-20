// Phase 0.5 — Golden CAT-trace replay regression harness.
//
// Loads a real K4 session recorded by capture_k4_golden.py, replays every
// CAT command through RadioState::parseCATCommand() in order, records the
// resulting emissions, and compares them against a committed expected
// emit log. Any change in the count or payload of any emitted signal
// causes the test to fail — that's the tripwire for Phase 1 regressions
// that would otherwise slip through per-handler unit tests (rollup signal
// reordering, sequence-dependent state bugs, etc.).
//
// The golden trace files live under tests/golden/ and are NOT checked in
// (they contain personal antenna labels and radio-specific identifiers).
// If the fixture is missing the test QSKIPs rather than failing — CI on a
// fresh checkout won't have the trace, and that's fine. The test is
// primarily a local safety net for refactor work.
//
// First-run workflow:
//   1. Record a trace with capture_k4_golden.py (writes session.cat.txt).
//   2. Run the test. On the first run the expected emit log is absent;
//      the harness writes session.emits.expected.txt from the live replay
//      and QSKIPs with a message saying "baseline created".
//   3. Inspect the file; if it looks right, leave it in place.
//   4. Subsequent runs diff against it. Any drift is reported.

#include "models/radiostate.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QTest>
#include <QTextStream>
#include <memory>

// Resolves to <project-root>/tests/golden/, where capture_k4_golden.py writes
// its .cat.txt outputs. We locate it by walking up from the test executable
// directory — CMake places test binaries at <build>/tests/, so ../../tests/
// from the binary finds the source tree location.
static QString goldenDir() {
    QDir d(QCoreApplication::applicationDirPath());
    // Walk up until we find a sibling "tests/golden" we can stat.
    for (int i = 0; i < 5; ++i) {
        const QString candidate = d.absoluteFilePath("tests/golden");
        if (QFileInfo(candidate).isDir())
            return candidate;
        if (!d.cdUp())
            break;
    }
    // Fallback: compile-time path from CMake build — test binaries live at
    // <build>/tests/, so ../.. reaches <source-root> where CMake was invoked.
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../tests/golden");
}

// Serialize each signal's full emit history into a stable, human-readable
// text format. Format per-signal:
//
//     <signalName> count=<n>
//       [i=0] <arg0>, <arg1>, ...
//       [i=1] <arg0>, <arg1>, ...
//
// Signals are sorted alphabetically so small reordering of unrelated
// signals doesn't cascade into a whole-file diff. Signals with zero
// emissions are included (as count=0) so a regression that stops emitting
// a signal altogether shows up as a count-delta, not a missing line.
static QString serializeEmitLog(const QMap<QString, QSignalSpy *> &spies) {
    QStringList signalNames = spies.keys();
    signalNames.sort();
    QString out;
    QTextStream ts(&out);
    for (const QString &name : signalNames) {
        QSignalSpy *spy = spies.value(name);
        ts << name << " count=" << spy->count() << '\n';
        for (int i = 0; i < spy->count(); ++i) {
            ts << "  [" << i << "]";
            const QList<QVariant> &args = spy->at(i);
            for (const QVariant &a : args) {
                ts << ' ';
                // QVariant::toString handles most primitives. For enums
                // (Mode, AGCSpeed) QVariant stores the int value after
                // the signal arg is converted to QVariant via Qt's meta.
                ts << a.toString();
            }
            ts << '\n';
        }
    }
    return out;
}

class TestRadioStateGolden : public QObject {
    Q_OBJECT

private:
    // Replay a single trace file through a fresh RadioState and return the
    // serialized emit log. The input is the trace's full path to .cat.txt.
    QString replayAndSerialize(const QString &traceFile) {
        RadioState rs;

        // Attach a QSignalSpy to every signal on RadioState. QMetaMethod
        // iteration gives us the full list; we skip QObject's own signals
        // (destroyed, objectNameChanged) so the log only contains
        // RadioState-specific emissions.
        const QMetaObject *mo = rs.metaObject();
        QMap<QString, QSignalSpy *> spies;
        for (int i = 0; i < mo->methodCount(); ++i) {
            QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Signal)
                continue;
            const QString name = QString::fromLatin1(m.name());
            if (name == QLatin1String("destroyed") || name == QLatin1String("objectNameChanged"))
                continue;
            spies.insert(name, new QSignalSpy(&rs, m));
        }

        // Replay every non-empty line as a CAT command.
        QFile f(traceFile);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "cannot open trace:" << traceFile;
            qDeleteAll(spies);
            return QString();
        }
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty())
                continue;
            rs.parseCATCommand(line);
        }

        const QString log = serializeEmitLog(spies);
        qDeleteAll(spies);
        return log;
    }

private slots:
    void replayGoldenTraces_data() {
        QTest::addColumn<QString>("traceName");
        QTest::newRow("session") << "session";
        QTest::newRow("session_menus") << "session_menus";
    }

    void replayGoldenTraces() {
        QFETCH(QString, traceName);
        const QString dir = goldenDir();
        const QString traceFile = dir + "/" + traceName + ".cat.txt";
        const QString expectedFile = dir + "/" + traceName + ".emits.expected.txt";

        if (!QFileInfo::exists(traceFile)) {
            QSKIP(qPrintable(
                QString("trace file not present (local fixture); skipping. expected at: %1").arg(traceFile)));
        }

        const QString actual = replayAndSerialize(traceFile);
        QVERIFY2(!actual.isEmpty(), "emit log came back empty — replay or serialization failure");

        if (!QFileInfo::exists(expectedFile)) {
            // First run against this trace: create the baseline. The test
            // QSKIPs — a fresh baseline can't regress against itself.
            QFile out(expectedFile);
            QVERIFY2(out.open(QIODevice::WriteOnly | QIODevice::Text),
                     qPrintable(QString("cannot create baseline: %1").arg(expectedFile)));
            out.write(actual.toUtf8());
            out.close();
            QSKIP(qPrintable(QString("created fresh baseline at %1 — inspect and run again to lock it in. "
                                     "This file is local-only (tests/golden/ is gitignored).")
                                 .arg(expectedFile)));
        }

        // Compare actual vs expected. On mismatch, also write the actual
        // alongside for manual diff.
        QFile expected(expectedFile);
        QVERIFY(expected.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString expectedText = QString::fromUtf8(expected.readAll());

        if (actual != expectedText) {
            const QString actualFile = dir + "/" + traceName + ".emits.actual.txt";
            QFile af(actualFile);
            if (af.open(QIODevice::WriteOnly | QIODevice::Text)) {
                af.write(actual.toUtf8());
                af.close();
            }
            QFAIL(qPrintable(QString("emit log drifted from baseline.\n  expected: %1\n  actual  : %2\n"
                                     "Diff them locally to see which signal(s) changed.")
                                 .arg(expectedFile, actualFile)));
        }
    }
};

QTEST_MAIN(TestRadioStateGolden)
#include "test_radiostate_golden.moc"
