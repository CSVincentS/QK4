# settings/

`QSettings`-backed persistence surface.

## Files

- `radiosettings.{cpp,h}` — Radio profiles, DX cluster list, KPA1500 config, audio device preferences, UI geometry. Includes password obfuscation helpers.

## Scope

Every persisted value on disk goes through here. Adding a setting:

1. Add getter/setter on `RadioSettings`.
2. Settings file path + key are managed internally — no caller should touch `QSettings::` directly.
3. Settings are plain reads/writes; no async or atomic contract.

## Security note

Passwords are obfuscated (not encrypted) via the `obfuscatePassword` / `deobfuscatePassword` helpers. Good enough to keep them out of clear-text scanning on disk; not a defense against a determined attacker. If that matters to you, use OS keychain APIs instead.
