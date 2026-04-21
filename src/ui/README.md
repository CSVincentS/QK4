# ui/

UI widgets, popups, overlays, dialogs, and styling infrastructure. Files are grouped by role — the directory tells you what kind of thing a file is.

## Subdirectories

| Directory | Owns |
|---|---|
| `styling/` | Infrastructure used by every UI file: `K4Styles` (colors, dimensions, fonts, stylesheet helpers) and `K4PopupBase` (popup base class). |
| `dialogs/` | Modal-ish full windows: `OptionsDialog`, `RadioManagerDialog`, `MacroDialog`, `TextDecodeWindow`. |
| `pages/` | 8 `OptionsDialog` tab pages: about, rig control, audio input/output, CW keyer, DX cluster, KPOD, KPA1500. |
| `popups/` | 14 popup widgets, all inherit from `K4PopupBase` — band, mode, Fn, display, button row, RX EQ, antenna config, line in/out, mic input/config, VOX, SSB BW, keying weight. |
| `overlays/` | 6 overlays: menu (full-screen), mon/bal (side-control), cursor follow, DX spots (panadapter), side-control base. |
| `widgets/` | The everything-else bucket — VFO widgets, meters, menu bars, side panels, KPA1500 mini-panel, notification, etc. |

## Include convention

Explicit paths everywhere: `#include "ui/popups/bandpopupwidget.h"` — never `#include "bandpopupwidget.h"`. Matches how `models/`, `network/`, `controllers/` are included and keeps cross-subdirectory references visible in the include line.

## Source of truth for styling

`ui/styling/k4styles.h`. Reference tables in `docs/K4STYLES_REFERENCE.md`. Never inline hex colors / pixel font sizes; always use `K4Styles::Colors::*` and `K4Styles::Dimensions::*`.

## See also

- `PATTERNS.md` → "Adding a Popup Menu" (K4PopupBase recipe).
- `docs/K4STYLES_REFERENCE.md` — full color / font / helper reference.
- `CONVENTIONS.md` → "Styling" section.
