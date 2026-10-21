# Changelog

## 2.0.0 — 2026-10-21

First release of the maintained fork. Rewritten; behaviour of the keys is unchanged.

### Fixed
- Switchy could silently stop working: Windows removes a low-level keyboard hook
  that misses its deadline. The hook is now re-installed every 20 s and after
  unlock/resume, does no work itself, and its code stays resident (hard minimum
  working set, above-normal priority, EcoQoS off).
- Fast repeated Caps Lock could toggle Switchy off: the switch injected Alt+Shift
  and the next press read that Alt as physical. Only physical modifiers count now;
  our own injected keys are tagged and ignored, and sent in a single `SendInput`.
- Caps Lock, then Shift in pop-up mode left the layout switched.
- A lost Caps Lock release (lock screen, UAC) could leave Win held down.
- Starting a second copy while an elevated copy runs started a second hook.

### Added
- Tray icon (on/off state), menu: enable, pop-up mode, autostart, autostart as
  administrator (logon task, exe in Program Files), exit. Double-click toggles.
- The Windows input-language hotkey setting is honoured (Alt+Shift, Ctrl+Shift,
  grave accent; Win+Space when none is set).
- Unit tests for the key state machine; live test script; CI with GCC, clang and
  MSVC builds.

### Changed
- No C runtime: 18 KB executable importing only system DLLs.
- A second instance exits silently instead of showing an error.
- Pop-up mode is a tray setting (stored in `HKCU\Software\Switchy`); the `nopopup`
  argument still works.
