# dsp/

GPU-accelerated spectrum + waterfall rendering via Qt RHI (Metal / DX / Vulkan / GL).

## Files

- `panadapter_rhi.{cpp,h}` — Main panadapter (spectrum + waterfall). Click-tune, passband overlays, DX spot overlays, TX markers. ~1870 LOC (naturally large — RHI pipeline + buffer management).
- `minipan_rhi.{cpp,h}` — Per-VFO mini-pan widget. ~1065 LOC.
- `panadapter_constants.h` — Base texture / history dimensions (`BASE_WATERFALL_HISTORY = 1024`, `BASE_TEXTURE_WIDTH = 4096`).
- `rhi_utils.h` — Shared RHI helpers (color LUT size, texture builders).
- `shaders/` — 4 shader pairs (vert/frag): spectrum, spectrum_fill, waterfall, overlay. Compiled at build time via `qt6_add_shaders()` in `CMakeLists.txt`.

## Ownership

Owned by `SpectrumController` (`controllers/spectrumcontroller.cpp`).

## Data flow

K4 spectrum packets → `Protocol` → `ConnectionController` → `SpectrumController` → `PanadapterRhiWidget` / `MinipanRhiWidget`.

Spectrum data routing includes AR (auto-reference), #SCL (scale), #SPN (span), #REF (reference level), #WFC (waterfall color), #WFH (waterfall history) — all stored on `SpectrumDisplayState` in `models/radiostate/`.

## See also

- `memory/k4-pan-tier-map.md` — verified tier boundaries, sample rates, bin counts.
- `memory/k4-span-stepping.md` — K4 span dial stepping quirk (5→7 kHz skip).
- `controllers/spectrumcontroller.cpp` — all wiring + click-tune logic.
