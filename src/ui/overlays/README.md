# ui/overlays/

Widgets that paint on top of other widgets. Not popups — overlays are not self-dismissing; they're driven by app state.

## Files

- `menuoverlay` — Full-screen MEDF menu overlay. Driven by `MenuController`.
- `sidecontroloverlay` — Base class for the two mini overlays below.
- `monoverlay` / `baloverlay` — MON / BAL side-control mini overlays.
- `mousevfoindicator` — Cursor-follow overlay. Shows the frequency under the mouse on the panadapter.
- `dxspotoverlay` — DX cluster spots painted onto the panadapter.

## Pattern

Overlays typically:
1. Take a pointer to their host widget as parent (for paint-coordinate mapping).
2. Observe one or two RadioState signals; re-render via `update()`.
3. Don't own CAT dispatch — pure presentation.

## Ownership

- `menuoverlay` → `MenuController`.
- `mon/bal` overlays → `SideControlPanel` (host widget).
- `mousevfoindicator` + `dxspotoverlay` → `SpectrumController` / `PanadapterRhiWidget`.
