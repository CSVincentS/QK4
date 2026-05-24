# Tracing the HaliKey → KZ CW pipeline

A diagnostic recipe for investigating CW-keyer reports such as "extra dits at high
speed" on HaliKey (MIDI or V1.4). Captures the path from physical paddle edge through
the IambicKeyer state machine to the `KZ` bytes that hit the K4 over TLS.

## What gets logged

Two logging categories carry the relevant traces:

| Category | Source | Lines you'll see |
|---|---|---|
| `cw.keyer` | `src/hardware/iambickeyer.cpp` | `KEYER@HH:mm:ss.zzz [DIT down] ...`, `[ELEM start DIT] ...`, `[TIMER fired wasDit=1] ...`, `[PAUSE emit] ...`, `[IDLE]` |
| `cat.tx` | `src/network/tcpclient.cpp` | `TX@HH:mm:ss.zzz [ KZ.; ]`, `[ KZ-; ]`, `[ KZP0180; ]`, `[ KZ ; ]` (KZ-prefixed CAT only) |

The KPOD+ EP02 path also logs in the same `TX@` format via `hw.kpodplus` (`KZ@`)
and `cat.tx` (`TX@`); you can use the same recipe to A/B a KPOD+ session against a
HaliKey session of the same character at the same WPM.

## Enabling the trace

Run the app with:

```bash
QT_LOGGING_RULES="cw.keyer.debug=true;cat.tx.debug=true" \
  /Users/mikegarcia/development/QK4/build/QK4.app/Contents/MacOS/QK4 \
  2>&1 | tee /tmp/qk4-cw-trace.log
```

The category is off by default — when disabled, `qCDebug` short-circuits at the
category check so there is zero hot-path cost.

## What a clean "C" looks like at 20 WPM

A single character `C` (dah-dit-dah-dit) on a HaliKey MIDI paddle, with iambic
sending, should produce a trace pattern like this (timestamps shortened):

```
KEYER@... [DAH down] state=Idle  physD=0 physA=1 latchD=0 latchA=1
KEYER@... [PAUSE emit] elapsed=420ms                          ← (if any gap from last char)
KEYER@... [ELEM start DAH] interval=240ms ...
TX@...    [ KZP0420; ]                                        ← restartAfterPause emit
TX@...    [ KZ-; ]                                            ← elementStarted emit
KEYER@... [DIT down] state=PlayingDah ...                     ← squeeze begins
KEYER@... [TIMER fired wasDit=0] liveD=1 liveA=1 ... → both held → alternate
KEYER@... [ELEM start DIT] interval=120ms ...
TX@...    [ KZ.; ]
KEYER@... [TIMER fired wasDit=1] ... → both held → alternate
KEYER@... [ELEM start DAH] interval=240ms ...
TX@...    [ KZ-; ]
KEYER@... [DAH up] state=PlayingDah ...
KEYER@... [DIT up] state=PlayingDah ...
KEYER@... [TIMER fired wasDit=0] ... → squeeze-release IambicA → idle
KEYER@... [IDLE]
TX@...    [ KZ ; ]                                            ← characterSpace emit
```

Wire output (extract just the `TX@` lines): `KZP0420; KZ-; KZ.; KZ-; KZ ;` —
which is one of the worked examples in `memory/kz-protocol.md`.

## What an "extra dit" looks like

A phantom dit will show up as a `[TIMER fired ...] → ...` line where the latch was
set but the live paddle was already released. Specifically: look for

```
KEYER@... [DIT down] state=PlayingDit ...                     ← graze during active element
KEYER@... [DIT up]   state=PlayingDit ...                     ← release ≤ a few ms later
...
KEYER@... [TIMER fired wasDit=1] liveD=0 liveA=0 latchD=1 ... → same-paddle repeat dit
KEYER@... [ELEM start DIT] ...
TX@...    [ KZ.; ]                                            ← the extra dit
```

The signature is **`liveD=0 ... latchD=1` followed by a `same-paddle repeat dit`** decision.
That's the iambic memory latch converting a mid-element graze into an extra element.

If the trace shows no such pattern but the user still reports an extra dit, the
problem isn't in the keyer — look at the K4's reproduction of a correctly-emitted
stream, or at local sidetone bleeding back through the audio capture path.

## Correlating across categories

`KEYER@` and `TX@` use the same `HH:mm:ss.zzz` wall-clock format. A diff between
the timestamp of `[ELEM start DIT]` and the following `TX@... [ KZ.; ]` is the
end-to-end latency from keyer-thread emit decision to TCP wire write — typically
sub-millisecond, but useful for diagnosing event-queue backups under load.

## Disabling cleanly

`unset QT_LOGGING_RULES` or omit the variable. Nothing else is needed.
