# Changelog

## 2.1.0 — 2026-10-21

Fixes the upstream issues that 2.0.0 did not cover yet.

### Fixed
- With Windows set to "Turn off Caps Lock: press Shift", Shift+Caps Lock could turn
  Caps Lock on but not off (erryox/Switchy#5, #2). If Caps Lock stays on after the
  toggle, Switchy taps the Shift you are not holding.

### Changed
- Caps Lock, then Alt now works like Alt+Caps Lock (turns Switchy off), the same way
  Caps Lock, then Shift already worked like Shift+Caps Lock (noted by the original
  author in erryox/Switchy#14).
- The live test checks after every scenario that the Win key is not left held
  (the cause of erryox/Switchy#15: arrows maximizing windows in PyCharm).

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
- The hidden window obeyed WM_CLOSE from other programs, so another app could
  quit Switchy. Only the tray menu exits now. The last exit reason is kept in
  `HKCU\Software\Switchy\LastExit` for bug reports.

### Added
- Tray icon (on/off state), menu: enable, pop-up mode, autostart, autostart as
  administrator (logon task, exe in Program Files), exit. Double-click toggles.
- The Windows input-language hotkey setting is honoured (Alt+Shift, Ctrl+Shift,
  grave accent; Win+Space when none is set).
- Unit tests for the key state machine; live test script; CI with GCC, clang and
  MSVC builds.

### Changed
- No C runtime: 19 KB executable importing only system DLLs.
- A second instance exits silently instead of showing an error.
- Pop-up mode is a tray setting (stored in `HKCU\Software\Switchy`); the `nopopup`
  argument still works.
