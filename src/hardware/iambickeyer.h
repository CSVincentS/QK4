#ifndef IAMBICKEYER_H
#define IAMBICKEYER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <atomic>

/**
 * @brief Iambic A/B CW keyer state machine. Lives on `HardwareController::m_keyerThread`
 *        (HighPriority). Accepts physical paddle state from HaliKey via atomics
 *        (DirectConnection-safe from any thread), emits `elementStarted(isDit)` /
 *        `characterSpace()` / `restartAfterPause(ms)` / `keyingFinished()` for MainWindow to
 *        dispatch K4 KZ commands (see `memory/kz-protocol.md`).
 *
 * Latch semantics (see `iambickeyer.cpp::enterElement`): opposite-paddle latch preserves
 * brief taps that ended before the element timer fired — required for Iambic-A correctness.
 */
class IambicKeyer : public QObject {
    Q_OBJECT

public:
    enum Mode { IambicA, IambicB };
    Q_ENUM(Mode)

    explicit IambicKeyer(QObject *parent = nullptr);

    // All public methods are Q_INVOKABLE so they can be called cross-thread
    // via QMetaObject::invokeMethod or auto-queued signal connections.

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setMode(Mode mode);
    Q_INVOKABLE void setReversed(bool reversed);
    Q_INVOKABLE void setSpeed(int wpm);

    // Accept PHYSICAL paddle state — reversal applied internally.
    // These write atomic bools directly (safe from any thread via DirectConnection),
    // then post handlePaddleChange() to the keyer thread to wake from idle.
    void setDitPaddle(bool pressed);
    void setDahPaddle(bool pressed);

    // Emergency stop (disconnect, etc.)
    Q_INVOKABLE void stop();

    bool isKeying() const;

signals:
    void elementStarted(bool isDit);
    void characterSpace();
    void restartAfterPause(int ms);
    void keyingFinished();

private:
    enum State { Idle, PlayingDit, PlayingDah };

    void handlePaddleChange();
    void enterElement(bool isDit);
    void onTimerFired();
    void goIdle();

    // Read the current logical paddle state (applies reversal to atomics)
    bool ditDown() const;
    bool dahDown() const;

    QTimer *m_elementTimer;
    State m_state = Idle;
    Mode m_mode = IambicA;
    bool m_reversed = false;
    bool m_squeezed = false; // both paddles held during current element
    bool m_enabled = false;  // gated by connection state
    int m_ditMs = 60;        // 1200 / WPM
    QElapsedTimer m_idleSince;

    // Free-running monotonic clock used to measure paddle-press hold durations. Started in the
    // constructor; never reset. Read concurrently from the HaliKey worker thread (in setDit/DahPaddle)
    // — QElapsedTimer's methods are reentrant per Qt docs.
    QElapsedTimer m_pressClock;

    // Physical paddle state — written from HaliKey thread via DirectConnection,
    // read from keyer thread's timer. Atomics eliminate cross-thread queue delay
    // so onTimerFired() always sees real-time paddle state.
    std::atomic<bool> m_physDit{false};
    std::atomic<bool> m_physDah{false};

    // Paddle latches — capture any press during an active element so that
    // onTimerFired() doesn't miss a paddle-down that was released before
    // the element timer fired.  Set on key-down, cleared at element start.
    std::atomic<bool> m_ditLatch{false};
    std::atomic<bool> m_dahLatch{false};

    // Press timestamps (nsecsElapsed from m_pressClock). Written in setDitPaddle/setDahPaddle
    // ONLY when the corresponding latch transitions unset → set, so a bounce-press during an
    // already-latched window cannot overwrite the original press timestamp. Read on the matching
    // release path to compute hold duration vs kMinHoldNs.
    std::atomic<qint64> m_ditPressNs{0};
    std::atomic<qint64> m_dahPressNs{0};

    // Minimum-hold threshold for a press to count as "real" and keep its latch on release.
    // Anything shorter is treated as paddle bounce or accidental graze (the bounce signatures
    // captured in docs/halikey-cw-trace.md were all ≤ 4ms hold; comfortably below any deliberate
    // tap at practical WPM rates).
    static constexpr qint64 kMinHoldNs = 8'000'000; // 8 ms
};

#endif // IAMBICKEYER_H
