# HaliKey MIDI — Windows "stuck paddles" bug

**Status:** fixed (2026-05-23). Pending hardware verification on Windows + MIDI.
**Affects:** HaliKey **MIDI** variant on **Windows** only. V1.4 serial variant and macOS are unaffected.
**Symptom:** after Beta 2 (v0.6.0-beta.2), CW timing is better but the keyer
intermittently "gets stuck sending both dots and dashes" — i.e. both paddle
contacts latch on and the iambic keyer sends continuous dit-dah.

## Root cause — QK4's debounce, not WinMM

The defect is the debounce filter `acceptEdge()` in
`src/hardware/halikeydevice.cpp:14-23`:

```cpp
bool acceptEdge(bool raw, std::atomic<bool> &confirmed, std::atomic<qint64> &lastEdgeNs, qint64 nowNs) {
    if (raw == confirmed.load(...)) return false;                  // redundant — same state
    if (nowNs - lastEdgeNs.load(...) < DEBOUNCE_NS) return false;   // ← THE BUG
    lastEdgeNs.store(nowNs); confirmed.store(raw); return true;
}
```

`DEBOUNCE_NS = 3'000'000` ns = **3 ms** (`src/hardware/halikeydevice.h:66`).

Two flaws:

1. **`nowNs` is the *processing* timestamp, not the event timestamp.** It is
   `m_clock.nsecsElapsed()` read inside `onRawDit/Dah/Ptt()` — *after* the
   RtMidi callback → `QMetaObject::invokeMethod` marshal hop → worker-thread
   event loop. It debounces on how fast QK4 *processed* two edges, not how far
   apart the paddle physically moved.
2. **Rejecting an edge as a "bounce" drops a genuine state transition and does
   not update `confirmed`.** A dropped release leaves the paddle latched
   "pressed." A debounce must never discard a real transition.

## Why Windows-specific

- **macOS / CoreMIDI:** delivers note events promptly and individually. A dit
  at 20 WPM is ~60 ms; press and release reach `onRawDit()` ~60 ms apart — far
  outside the 3 ms window. Debounce never trips. Works.
- **Windows / WinMM:** WinMM MIDI input is lower-priority and **bursty** — it
  holds events and delivers them back-to-back when the scheduler runs. A press
  and release 60 ms apart on the wire get handed to RtMidi ~1 ms apart; they
  marshal across and the worker thread drains both lambdas in one pass, so
  `onRawDit(true)` then `onRawDit(false)` run **<3 ms apart in processing
  time** → the release is silently dropped → that paddle sticks "on." Both
  paddles stick → continuous iambic dit-dah.

This also explains "timing is better but it gets stuck": Beta 2's marshaling
work improved *timing*, but the 3 ms processing-time debounce still eats
fast-arriving releases.

## MIDI has no electrical bounce

The 3 ms time-window debounce is meaningful only for the **V1.4 serial**
variant — raw RS-232 control-line transitions genuinely bounce. A MIDI HaliKey
sends already-debounced note-on/note-off from firmware; there is no electrical
bounce in a MIDI message stream, so time-debouncing it is pure harm.

## Recommended fix (original write-up)

Apply the time-window debounce to the **V1.4 serial worker only**. The MIDI
path keeps just the redundant-state filter (`raw == confirmed` → drop), which
is safe and never discards a real transition. V1.4 stays fully protected.

`acceptEdge()` is shared by all three lines and both variants; gate the
time-window check on device type (`m_deviceType == 1` is MIDI), or have the
MIDI worker's `onRaw*` path bypass it. (Per-event timestamps from RtMidi's
`deltaTime` would also work, but MIDI should simply not be time-debounced.)

## Resolution

We took the architectural rather than the patched-conditional route. The 3 ms
time gate was **removed entirely** from `HalikeyDevice`; same-direction dedupe
is the only thing that remains. Rationale:

- Conditioning the time gate on `m_deviceType` would have worked but coupled
  `HalikeyDevice` to worker-variant logic and left a magic number in a class
  that should be a pure cross-thread fan-out.
- The V1.4 serial worker already has its own count-based debounce
  (`DEBOUNCE_COUNT=2` across ≥500 µs reads, in `halikeyv14worker.cpp`'s
  `monitorLoop`). That confirms ≥1 ms of mechanical stability before emit —
  more than enough for typical sub-millisecond contact bounce. The 3 ms
  device-layer gate was redundant for serial and harmful for MIDI.
- The MIDI variant doesn't need any debounce on our side — the HaliKey
  firmware delivers logical Note On/Off events, not raw contact transitions.

The dedupe helper (`acceptEdge`) was extracted into `src/hardware/halikey_edge.h`
so it can be unit-tested in isolation without linking RtMidi or QSerialPort.
Regression tests live in `tests/test_halikeydevice.cpp` — notably
`backToBackPressReleaseBothAccepted`, which would have failed against the
pre-fix code.

V1.4 worker carries a `// WHY:` comment marking its count-based debounce as
the sole bounce defense in the serial path. Do not relax it without restoring
an equivalent gate elsewhere.

## Vetting of an earlier external analysis

A prior analysis blamed WinMM jitter and proposed RtMidi API changes. Assessment:

- "RtMidi on Windows uses WinMM" — **correct**; WinMM is the only MIDI input
  API RtMidi has on Windows.
- "WinMM has ~10-15 ms jitter" — roughly right, and it is the *trigger* — but
  the bug is QK4's debounce, not WinMM. Bursty delivery is normal WinMM
  behavior that QK4 mishandles.
- "Switch WinMM → WASAPI/DirectSound" — **wrong**; those are audio APIs. There
  is no WASAPI-MIDI or DirectSound-MIDI.
- "Change `RtMidi::WINDOWS_MM` → `RtMidi::UNSPECIFIED`" — **no-op**; QK4
  already uses the default (`std::make_unique<RtMidiIn>()` with no API arg
  *is* `UNSPECIFIED`, `halikeymidiworker.cpp:30`), and on Windows
  `UNSPECIFIED` resolves to WinMM regardless.
- "Set callback-thread priority" — won't fix it; the RtMidi callback thread is
  internal and QK4 already marshals off it. A WinMM burst is still drained
  back-to-back and the debounce still drops the release.

## Secondary item to confirm (separate from this bug)

`HaliKeyMidiWorker::handleMidiMessage()` (`halikeymidiworker.cpp:125-133`):
the MoMIDI branch treats *every* `0x90` Note-On as a key-down. That is correct
only if the MoMIDI firmware sends `0x80` Note-Off for releases (not `0x90`
velocity-0). The `0x80` handler is present, so it is likely fine — verify
against the MoMIDI spec.

## Key files

- `src/hardware/halikeydevice.cpp` — `acceptEdge()` debounce, `onRaw*` handlers
- `src/hardware/halikeydevice.h:66` — `DEBOUNCE_NS = 3 ms`
- `src/hardware/halikeymidiworker.cpp` — RtMidi callback, marshal hop, note decode
- `src/hardware/halikeyv14worker.cpp` — V1.4 serial worker (debounce stays here)
