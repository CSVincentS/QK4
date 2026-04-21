# hardware/

USB / serial / MIDI device wrappers. Owned by `controllers/hardwarecontroller.cpp`.

## Files

- `kpoddevice.{cpp,h}` — KPOD tuning knob via hidapi. Main-thread (timing not critical). Device detection runs asynchronously at startup via `QTimer::singleShot(0, ...)` so the app window appears immediately; consumers observe `deviceInfoReady()` before reading `isDetected()`.
- `halikeydevice.{cpp,h}` — HaliKey CW paddle. Delegates to one of 3 platform workers; owns its own `m_workerThread`.
- `halikeyworkerbase.{cpp,h}` — Abstract base for platform workers. `prepareShutdown()` is the escape hatch for the Linux variant's blocking ioctl.
- `halikeyv14worker.{cpp,h}` — V1.4 hardware-protocol worker (serial).
- `halikeymidiworker.{cpp,h}` — MIDI-variant worker.
- `iambickeyer.{cpp,h}` — Iambic A/B CW keyer state machine. `HighPriority` thread, atomic paddle state.

## Threading

- `IambicKeyer` on `HardwareController::m_keyerThread` (HighPriority).
- `SidetoneGenerator` on `HardwareController::m_sidetoneThread`.
- `HalikeyDevice` has its own `m_workerThread` for platform-worker variants.
- `KpodDevice` stays on the main thread.

Six `QThread` creations total across the app; four of them are in or adjacent to this directory.

## Keyer flow

HaliKey paddle → platform worker (thread) → IambicKeyer::setDitPaddle / setDahPaddle (atomics, DirectConnection) → IambicKeyer state machine (keyer thread) → KZ CAT commands out + SidetoneGenerator enqueue.

## See also

- `memory/kz-protocol.md` — KZ command protocol verified from K4/0 pcap.
- `memory/cw-keyer-thread-plan.md` — SidetoneGenerator thread migration rationale.
- `memory/kpod-linux.md` — hidapi platform differences, Linux fixes.
- `memory/threading-audit.md` — full thread map.
