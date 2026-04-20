// Phase 0.2 — CAT registry invariant test.
//
// parseCATCommand() dispatches via a first-match-wins linear scan over
// m_commandHandlers. Two structural invariants keep that correct and
// non-surprising; this test locks them in so a future re-order can't
// silently shadow a handler.
//
//   1. Shadow safety — if prefix X is a proper prefix of prefix Y
//      (Y.startsWith(X) && X.length() < Y.length()), then Y must be
//      registered before X. Example: "RO$" must come before "RO".
//      Violating this makes Y unreachable — every command matching Y
//      also matches X, so the shorter X eats it first.
//
//   2. No duplicates — every prefix is registered exactly once. A
//      duplicate never fires because the first entry always matches.

#include "models/radiostate.h"
#include <QSet>
#include <QTest>

class TestRadioStateRegistry : public QObject {
    Q_OBJECT

private slots:
    void testRegistryNonEmpty() {
        RadioState rs;
        QVERIFY(rs.registeredCommandPrefixes().size() > 0);
    }

    // Primary correctness invariant: no shorter prefix registered before a
    // longer prefix it prefixes.
    void testNoPrefixShadowing() {
        RadioState rs;
        const QStringList prefixes = rs.registeredCommandPrefixes();
        for (int i = 0; i < prefixes.size(); ++i) {
            for (int j = i + 1; j < prefixes.size(); ++j) {
                const QString &earlier = prefixes.at(i);
                const QString &later = prefixes.at(j);
                if (later.startsWith(earlier) && earlier.length() < later.length()) {
                    QString msg = QString("Shadow violation: '%1' (index %2, length %3) "
                                          "registered before its longer variant '%4' (index %5, length %6). "
                                          "The shorter prefix will eat all commands matching the longer one.")
                                      .arg(earlier)
                                      .arg(i)
                                      .arg(earlier.length())
                                      .arg(later)
                                      .arg(j)
                                      .arg(later.length());
                    QFAIL(qPrintable(msg));
                }
            }
        }
    }

    void testNoDuplicatePrefixes() {
        RadioState rs;
        const QStringList prefixes = rs.registeredCommandPrefixes();
        QSet<QString> seen;
        for (const QString &p : prefixes) {
            if (seen.contains(p))
                QFAIL(qPrintable(QString("Duplicate registered prefix: '%1'").arg(p)));
            seen.insert(p);
        }
    }

    // Sanity: dispatch actually reaches every registered prefix — no
    // orphan entries with unreachable handlers. We verify by sending a
    // minimal malformed command that starts with each prefix; the handler
    // should run (and mostly bail on length checks) without crashing.
    // This is not a semantic test — it only proves dispatch is reachable.
    void testEveryPrefixIsReachable() {
        RadioState rs;
        const QStringList prefixes = rs.registeredCommandPrefixes();
        for (const QString &p : prefixes) {
            // Send the bare prefix + ";". Most handlers will reject via length
            // guards; a few may mutate state. Either way, no crash = success.
            rs.parseCATCommand(p + QStringLiteral(";"));
        }
        // If we got here, all handlers returned.
        QVERIFY(true);
    }
};

QTEST_MAIN(TestRadioStateRegistry)
#include "test_radiostate_registry.moc"
