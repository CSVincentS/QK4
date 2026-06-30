#ifndef PANADAPTER_CONSTANTS_H
#define PANADAPTER_CONSTANTS_H

// Shared rendering constants for PanadapterRhiWidget and MiniPanRhiWidget.
// Colors and theme values live in K4Styles; these are GPU rendering parameters.

namespace PanadapterConstants {

// RTTY shift (Hz) — fixed at 170 Hz for standard amateur RTTY.
// FSK Mark-Tone is user-configurable from K4 front panel (default 915 Hz);
// the shift between Mark and Space is always 170 Hz.
constexpr float RttyShiftHz = 170.0f;
constexpr float RttyHalfShiftHz = RttyShiftHz / 2.0f;

// Fixed spectrum grid cell size in LOGICAL px (scaled by devicePixelRatio at draw time so cells
// stay a constant VISIBLE size — unlike the marker widths below, which are physical px). The grid
// holds this cell size and adds/removes cells as the window or spectrum/waterfall ratio changes,
// so cells never stretch. Values are the measured default first-run cell size (logical panadapter
// width 1095/16 and spectrum height 177/8 at the 1340×840 default window) so the default view is
// unchanged (16×8 cells); larger windows / spectrum just get more cells of the same size.
constexpr float GridCellWidthPx = 68.44f;
constexpr float GridCellHeightPx = 22.14f;

// Line widths (pixels)
constexpr float MarkerLineWidth = 2.0f;
constexpr float RttyDashLineWidth = 1.5f;
constexpr float PassbandEdgeWidth = 2.0f;

// Dash pattern (pixels)
constexpr float DashLengthPx = 6.0f;
constexpr float DashGapPx = 4.0f;

// Passband alpha (0–255)
constexpr int PassbandFillAlpha = 64;
constexpr int PassbandEdgeAlpha = 180;

// MiniPan passband alpha — higher than main pan for visibility at small scale
constexpr int MiniPanFillAlpha = 100;

// MiniPan span Hz — Narrow used for CW/FSK/AFSK, Wide for all others
constexpr int SpanNarrowHz = 2000;
constexpr int SpanWideHz = 10000;

} // namespace PanadapterConstants

#endif // PANADAPTER_CONSTANTS_H
