# ui/dialogs/

Top-level auxiliary windows. Not modals in the strict Qt sense (most are non-blocking), but they're full windows rather than inline widgets.

## Files

- `optionsdialog` — Tabbed preferences. Hosts the 8 `ui/pages/` tab pages.
- `radiomanagerdialog` — K4 server picker. Auto-discovery (mDNS scan) + manual host/port entry.
- `macrodialog` — Full-screen macro editor (PF1–4 / F1–12).
- `textdecodewindow` — FT8 / PSK decoder text window.

## Pattern

1. Dialogs are typically lazy-created on first open, then kept alive for reuse (e.g., `m_optionsDialog` on MainWindow is `nullptr` until first `showOptions()`).
2. Use `K4Styles::Dialog::*` for every label/combo/line-edit styling — never inline `QString("color: %1; ...")`.
3. Persist any user-edited state via `RadioSettings` (`settings/`).

## See also

- `ui/pages/README.md` — tab pages hosted by OptionsDialog.
- `settings/radiosettings.h` — persistence.
