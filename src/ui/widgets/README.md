# ui/widgets/

Top-level app widgets that aren't popups, pages, overlays, dialogs, or styling infrastructure. The "everything else" bucket.

## Files

### Main display (5)
- `vfowidget`, `vforowwidget` — dual-VFO display (freq + mini-pan per VFO).
- `frequencydisplaywidget` — numeric frequency renderer (tabular figures, RIT/XIT aware).
- `filterindicatorwidget` — filter bandwidth indicator on VFO row.
- `txmeterwidget` — multifunction S / Po / ALC / COMP / SWR / Id meter.

### Menu bars + panels (5)
- `bottommenubar` — MENU, Fn, DISPLAY, BAND, PTT.
- `featuremenubar` — +/- control bar for ATTN / NB / NR / MANUAL NOTCH.
- `sidecontrolpanel` — left panel: volume, AF/RF/MIC gain, squelch, BW/SHFT.
- `rightsidepanel` — right panel: function buttons, memory, KPA1500 mini-panel slot.
- `kpa1500minipanel` — KPA1500 status + button slab for the right side panel.

### Small controls (4)
- `dualcontrolbutton` — custom split-button for paired controls.
- `controlgroupwidget` / `togglegroupwidget` — radio-button-like grouping for option sets.
- `displaymenubutton` — DISPLAY popup trigger button.

### Utility / misc (3)
- `nethealthwidget` — TCP health LED for the top status bar.
- `notificationwidget` — Toast notifications (K4 errors, etc.).
- `wheelaccumulator` — Shared QWheelEvent delta accumulator for fractional wheel steps.

## Pattern

Most widgets here are either:
1. **Custom-painted** — override `paintEvent()`, use `QPainter` + `K4Styles::Fonts::paintFont()` + `K4Styles::buttonGradient()`.
2. **Composite Qt widgets** — `QHBoxLayout` / `QVBoxLayout` of child `QPushButton` / `QLabel` / `QSlider` with `K4Styles::*` stylesheets applied.

**Font sizing rule**: `setPixelSize()` with `K4Styles::Dimensions::FontSize*` — never `setPointSize()`.

## See also

- `ui/styling/k4styles.h` — every visual constant.
- `docs/K4STYLES_REFERENCE.md` — full styling reference.
