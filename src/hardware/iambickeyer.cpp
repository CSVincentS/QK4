#include "hardware/iambickeyer.h"

#include <QLoggingCategory>
#include <QTime>

// Trace category for the CW keyer state machine. Disabled by default; enable with
// QT_LOGGING_RULES="cw.keyer.debug=true" to capture paddle edges, element-start decisions,
// timer-expiry decisions, and idle transitions. Pair with cat.tx category to correlate
// keyer state with on-wire KZ commands when diagnosing extra-dit reports.
Q_LOGGING_CATEGORY(cwKeyer, "cw.keyer")

namespace {
QString nowMs() {
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}
} // namespace

IambicKeyer::IambicKeyer(QObject *parent) : QObject(parent) {
    m_elementTimer = new QTimer(this);
    m_elementTimer->setSingleShot(true);
    m_elementTimer->setTimerType(Qt::PreciseTimer);
    connect(m_elementTimer, &QTimer::timeout, this, &IambicKeyer::onTimerFired);
    m_pressClock.start();
}

void IambicKeyer::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled)
        stop();
}

void IambicKeyer::setMode(Mode mode) {
    m_mode = mode;
}

void IambicKeyer::setReversed(bool reversed) {
    m_reversed = reversed;
}

void IambicKeyer::setSpeed(int wpm) {
    if (wpm > 0)
        m_ditMs = 1200 / wpm;
}

void IambicKeyer::setDitPaddle(bool pressed) {
    // Write atomic immediately (called from HaliKey worker thread via DirectConnection).
    // Release ordering pairs with the acquire loads in ditDown()/handlePaddleChange so the
    // keyer thread sees any side effects in this stack (none today, but documents intent
    // and is correct under weak memory models like Apple Silicon).
    const qint64 now = m_pressClock.nsecsElapsed();
    m_physDit.store(pressed, std::memory_order_release);

    qint64 holdNs = -1;
    bool bounceFiltered = false;
    if (pressed) {
        // Only stamp the press timestamp on a fresh transition unset→set. A bounce-press
        // arriving while the latch is already true must NOT overwrite the legitimate press time
        // (that would let a quick subsequent release wrongly clear a real latch).
        if (!m_ditLatch.exchange(true, std::memory_order_acq_rel))
            m_ditPressNs.store(now, std::memory_order_release);
    } else {
        // Release: hold-duration gate. Anything shorter than kMinHoldNs is treated as bounce
        // or accidental graze and the latch is cleared so the next element timer doesn't fire
        // a phantom element. See docs/halikey-cw-trace.md for the captured signatures.
        const qint64 pressNs = m_ditPressNs.load(std::memory_order_acquire);
        holdNs = now - pressNs;
        if (holdNs < kMinHoldNs) {
            m_ditLatch.store(false, std::memory_order_release);
            bounceFiltered = true;
        }
    }

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [DIT " << (pressed ? "down" : "up")
                                         << (bounceFiltered ? " bounce-filtered" : "") << "] state=" << m_state
                                         << " physD=" << m_physDit.load(std::memory_order_acquire)
                                         << " physA=" << m_physDah.load(std::memory_order_acquire)
                                         << " latchD=" << m_ditLatch.load(std::memory_order_acquire)
                                         << " latchA=" << m_dahLatch.load(std::memory_order_acquire)
                                         << (holdNs >= 0 ? QString(" hold=%1ms").arg(holdNs / 1'000'000.0, 0, 'f', 1)
                                                         : QString());

    // Post handlePaddleChange to keyer thread to wake from idle.
    // If keyer is already running, the timer will read the atomic directly.
    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

void IambicKeyer::setDahPaddle(bool pressed) {
    const qint64 now = m_pressClock.nsecsElapsed();
    m_physDah.store(pressed, std::memory_order_release);

    qint64 holdNs = -1;
    bool bounceFiltered = false;
    if (pressed) {
        if (!m_dahLatch.exchange(true, std::memory_order_acq_rel))
            m_dahPressNs.store(now, std::memory_order_release);
    } else {
        const qint64 pressNs = m_dahPressNs.load(std::memory_order_acquire);
        holdNs = now - pressNs;
        if (holdNs < kMinHoldNs) {
            m_dahLatch.store(false, std::memory_order_release);
            bounceFiltered = true;
        }
    }

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [DAH " << (pressed ? "down" : "up")
                                         << (bounceFiltered ? " bounce-filtered" : "") << "] state=" << m_state
                                         << " physD=" << m_physDit.load(std::memory_order_acquire)
                                         << " physA=" << m_physDah.load(std::memory_order_acquire)
                                         << " latchD=" << m_ditLatch.load(std::memory_order_acquire)
                                         << " latchA=" << m_dahLatch.load(std::memory_order_acquire)
                                         << (holdNs >= 0 ? QString(" hold=%1ms").arg(holdNs / 1'000'000.0, 0, 'f', 1)
                                                         : QString());

    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

bool IambicKeyer::ditDown() const {
    bool dit = m_physDit.load(std::memory_order_acquire);
    bool dah = m_physDah.load(std::memory_order_acquire);
    return m_reversed ? dah : dit;
}

bool IambicKeyer::dahDown() const {
    bool dit = m_physDit.load(std::memory_order_acquire);
    bool dah = m_physDah.load(std::memory_order_acquire);
    return m_reversed ? dit : dah;
}

void IambicKeyer::handlePaddleChange() {
    if (!m_enabled)
        return;

    bool dit = ditDown() ||
               (m_reversed ? m_dahLatch.load(std::memory_order_acquire) : m_ditLatch.load(std::memory_order_acquire));
    bool dah = dahDown() ||
               (m_reversed ? m_ditLatch.load(std::memory_order_acquire) : m_dahLatch.load(std::memory_order_acquire));

    // Track squeeze state during active element
    if (m_state != Idle && dit && dah)
        m_squeezed = true;

    // Start keying if idle and any paddle is down
    if (m_state == Idle) {
        if (dit && !dah)
            enterElement(true);
        else if (dah && !dit)
            enterElement(false);
        else if (dit && dah)
            enterElement(true); // squeeze from idle starts with dit
    }
}

void IambicKeyer::enterElement(bool isDit) {
    // Transitioning from idle — emit pause duration before the element
    if (m_state == Idle && m_idleSince.isValid()) {
        int elapsed = static_cast<int>(m_idleSince.elapsed());
        if (elapsed <= 2000) {
            qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [PAUSE emit] elapsed=" << elapsed << "ms";
            emit restartAfterPause(elapsed);
        } else {
            qCDebug(cwKeyer).noquote().nospace()
                << "KEYER@" << nowMs() << " [PAUSE skip] elapsed=" << elapsed << "ms (>2000)";
        }
    }

    m_state = isDit ? PlayingDit : PlayingDah;
    m_squeezed = false;

    // WHY only the same-element latch clears here: the latch we consume represents the
    // just-played element's paddle; leaving the opposite latch alone preserves any
    // cross-paddle press that happened during the previous element. Without this, brief
    // Iambic-A taps would be dropped — the paddle released before the element timer fired
    // but the opposite-paddle latch is what lets the keyer still emit the character.
    if (isDit != m_reversed)
        m_ditLatch.store(false, std::memory_order_relaxed);
    else
        m_dahLatch.store(false, std::memory_order_relaxed);

    // Re-check current paddles for squeeze detection within this element
    if (ditDown() && dahDown())
        m_squeezed = true;

    // Dit = 1 unit on + 1 unit off = 2 ditMs; Dah = 3 units on + 1 unit off = 4 ditMs
    int interval = isDit ? m_ditMs * 2 : m_ditMs * 4;
    m_elementTimer->start(interval);

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [ELEM start " << (isDit ? "DIT" : "DAH")
                                         << "] interval=" << interval
                                         << "ms physD=" << m_physDit.load(std::memory_order_acquire)
                                         << " physA=" << m_physDah.load(std::memory_order_acquire)
                                         << " latchD=" << m_ditLatch.load(std::memory_order_acquire)
                                         << " latchA=" << m_dahLatch.load(std::memory_order_acquire)
                                         << " squeezed=" << m_squeezed;

    emit elementStarted(isDit);
}

void IambicKeyer::onTimerFired() {
    bool liveDit = ditDown();
    bool liveDah = dahDown();

    // Check both live paddle state AND latch (paddle was pressed during this element).
    // Acquire ordering pairs with setDitPaddle/setDahPaddle's release stores from the
    // HaliKey worker thread. Reads of latches stored on this same (keyer) thread don't
    // need a barrier — but the keyer can't distinguish at the load site, so the stricter
    // acquire is used uniformly. Trivially compiles to the same code as relaxed on x86
    // and a one-instruction barrier on ARM.
    bool latchDit =
        m_reversed ? m_dahLatch.load(std::memory_order_acquire) : m_ditLatch.load(std::memory_order_acquire);
    bool latchDah =
        m_reversed ? m_ditLatch.load(std::memory_order_acquire) : m_dahLatch.load(std::memory_order_acquire);
    bool dit = liveDit || latchDit;
    bool dah = liveDah || latchDah;
    bool wasDit = (m_state == PlayingDit);

    auto traceDecision = [&](const char *branch) {
        qCDebug(cwKeyer).noquote().nospace()
            << "KEYER@" << nowMs() << " [TIMER fired wasDit=" << wasDit << "] liveD=" << liveDit << " liveA=" << liveDah
            << " latchD=" << latchDit << " latchA=" << latchDah << " squeezed=" << m_squeezed
            << " mode=" << (m_mode == IambicB ? "B" : "A") << " → " << branch;
    };

    // Squeeze release: both paddles physically released while squeeze was active.
    // Bypass latches — use Iambic A/B mode rules instead.  Without this guard,
    // a stale opposite-paddle latch would produce an unwanted extra element in
    // Iambic A mode.
    if (m_squeezed && !liveDit && !liveDah) {
        if (m_mode == IambicB) {
            traceDecision("squeeze-release IambicB → opposite element");
            enterElement(!wasDit);
        } else {
            traceDecision("squeeze-release IambicA → idle");
            goIdle();
        }
        return;
    }

    if (dit && dah) {
        traceDecision("both held → alternate");
        enterElement(!wasDit);
    } else if (wasDit && dah) {
        traceDecision("cross-paddle dit→dah");
        enterElement(false);
    } else if (!wasDit && dit) {
        traceDecision("cross-paddle dah→dit");
        enterElement(true);
    } else if (wasDit && dit) {
        traceDecision("same-paddle repeat dit");
        enterElement(true);
    } else if (!wasDit && dah) {
        traceDecision("same-paddle repeat dah");
        enterElement(false);
    } else if (m_squeezed && m_mode == IambicB) {
        traceDecision("IambicB squeeze-memory → extra opposite");
        enterElement(!wasDit);
    } else {
        traceDecision("nothing held → idle");
        goIdle();
    }
}

void IambicKeyer::goIdle() {
    m_state = Idle;
    m_elementTimer->stop();
    m_squeezed = false;
    m_ditLatch.store(false, std::memory_order_relaxed);
    m_dahLatch.store(false, std::memory_order_relaxed);
    m_idleSince.start();

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [IDLE]";

    emit characterSpace();
    emit keyingFinished();
}

void IambicKeyer::stop() {
    if (m_state != Idle) {
        m_state = Idle;
        m_elementTimer->stop();
        m_squeezed = false;
        m_physDit.store(false, std::memory_order_relaxed);
        m_physDah.store(false, std::memory_order_relaxed);
        m_ditLatch.store(false, std::memory_order_relaxed);
        m_dahLatch.store(false, std::memory_order_relaxed);
        emit keyingFinished();
    }
}

bool IambicKeyer::isKeying() const {
    return m_state != Idle;
}
