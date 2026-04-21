# ui/styling/

Shared styling infrastructure. Used by every other `ui/` subdirectory.

## Files

- `k4styles.{cpp,h}` — Color palette, dimension constants, font helpers, stylesheet functions, `Dialog::` cached-QString helpers. **Single source of truth for every visual constant.**
- `k4popupbase.{cpp,h}` — Base class for every popup (see `ui/popups/`). Handles window flags, shadow rendering, Escape/click-outside dismissal, screen-boundary detection.

## Rule

Never inline hex colors or pixel font sizes in widget code. Always reference `K4Styles::Colors::*` and `K4Styles::Dimensions::*`. Review-rejected otherwise.

## Reference

- `docs/K4STYLES_REFERENCE.md` — complete reference for every color, dimension, helper, and `Dialog::` function.
- Sanity grep: `rg 'constexpr const char \*' src/ui/styling/k4styles.h` lists every color constant.
