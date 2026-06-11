# hardware/

USB / serial / MIDI device wrappers. Owned by `controllers/hardwarecontroller.cpp`.

## Files

- `kpoddevice.{cpp,h}` — KPOD tuning knob via hidapi. Main-thread (timing not critical). Device detection runs asynchronously at startup via `QTimer::singleShot(0, ...)` so the app window appears immediately; consumers observe `deviceInfoReady()` before reading `isDetected()`.
- `kpodplusdevice.{cpp,h}` — KPOD+ tuning knob + CW keyer via libusb. Encoder/buttons/rocker polling on the main thread; keyer output read on a dedicated worker thread. Configurable keyer parameters (speed, pitch, iambic mode, paddle orientation, encode mode, stuck timeout) sent to device on change.
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
- `KpodPlusDevice` polls on the main thread; keyer reader runs on `m_ep02Thread`.

Seven `QThread` creations total across the app; five of them are in or adjacent to this directory.

## Keyer flow

HaliKey paddle → platform worker (thread) → IambicKeyer::setDitPaddle / setDahPaddle (atomics, DirectConnection) → IambicKeyer state machine (keyer thread) → KZ CAT commands out + SidetoneGenerator enqueue.

When KPOD+ is active, the HaliKey → IambicKeyer → KZ/Sidetone path is suppressed. KPOD+ owns the entire CW chain: paddle → onboard keyer → sidetone → KZ output forwarded directly to K4.

## V1.4 serial latency (USB-serial bridge latency timer)

FTDI-class USB-serial bridges batch modem-status (CTS/DSR/DCD) updates on a driver-side
latency timer that defaults to **16 ms**. That sits *upstream* of QK4's 1 ms poll
(`halikeyv14worker.cpp`), so paddle edges can reach the app up to 16 ms late regardless of
application code. Lowering it to 1 ms removes that delay:

- **Windows**: Device Manager → Ports (COM & LPT) → the HaliKey COM port → Properties →
  Port Settings → Advanced → Latency Timer (msec) = **1**. Confirmed effective by a user
  in the field.
- **Linux**: `echo 1 | sudo tee /sys/bus/usb-serial/devices/<dev>/latency_timer` — resets
  on re-plug; use a udev rule for persistence.
- **macOS**: not user-tunable with the stock driver.

The MIDI variant is unaffected (no serial bridge in the path).

## See also

- `memory/kz-protocol.md` — KZ command protocol verified from K4/0 pcap.
- `memory/cw-keyer-thread-plan.md` — SidetoneGenerator thread migration rationale.
- `memory/kpod-linux.md` — hidapi platform differences, Linux fixes.
- `memory/kpodplus-protocol.md` — KPOD+ protocol, keyer output format, forwarding architecture.
- `memory/threading-audit.md` — full thread map.
