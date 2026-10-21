# Changelog

## 2.2.2 — 2026-10-21

The rest of the Codex review. Still no new features.

### Changed
- The keyboard hook has a thread of its own (high priority) that does nothing
  else, as Microsoft advises for low-level hooks. Before, it shared the thread
  with the tray icon: while Explorer was busy (right after sign-in) every key
  press in the session could wait for Switchy. The state shared by the two
  threads is behind a lock.
- 22 KB (the hook thread and the fixes below cost 1.5 KB).

### Fixed
- A modifier you already hold is used, not pressed and released again: with
  Ctrl+Shift as the language hotkey, holding Ctrl and pressing Caps Lock no
  longer releases your Ctrl; Win held in pop-up mode stays held. The Shift tap
  that turns Caps Lock off waits while both Shift keys are held.
- Pop-up mode: a layout change that has to be undone (Caps Lock, then Shift or
  Alt) goes back to the exact layout from before, even when the language hotkey
  cycles a different set than Win+Space (falls back to the old way if the app
  ignores the request). Nothing is undone if the flyout never opened.
- A Win release that Windows blocked (an admin window took the focus) is
  retried, so Win doesn't stay held.
- Handing over to the admin task waits until the new copy has its hook, not
  just until it started: if it fails on the way, this copy keeps running.
- Turning off the admin autostart sets the normal one first and removes the
  task only then (and puts things back if that fails).
- An exe path longer than MAX_PATH is refused instead of being cut short.

### Tests
- The key sequence for each language hotkey (and the borrowed modifier) is a
  pure function with unit tests. CI runs the unit tests with gcc, clang and
  MSVC (/W4 /WX).
- The live test window has no system menu: Alt+Caps Lock reaches it as a lone
  Alt, which used to start a modal menu loop and hang the test.

## 2.2.1 — 2026-10-21

Review with Codex: no new features, everything works as before, only safer.

### Security
- The administrator autostart helper runs elevated, possibly from Downloads. It
  no longer lets files next to it run as administrator: `schtasks.exe` is
  started from System32 by full path, `wtsapi32.dll` (not a KnownDLL) is loaded
  from System32 at run time instead of being imported, and DLL search is
  limited to System32.
- The task XML is written into the admin-only `Program Files\Switchy` folder
  instead of `%TEMP%`, where a normal process could swap it before the elevated
  `schtasks` read it. Paths come from HKLM, the account is the token's SID
  (nothing taken from environment variables), text is XML-escaped.
- Updating the installed copy stages the new file first and rolls back on
  failure: a failed update never leaves the task pointing at a missing file.

### Fixed
- A Shift or Alt release that went to an admin window or the secure desktop
  (Alt+Tab into an elevated app, Ctrl+Alt+Del, a UAC prompt) is no longer
  remembered as "held": the next Caps Lock switched Caps Lock or turned Switchy
  off instead of switching the layout.
- Releasing one Shift (or Alt) while holding the other no longer forgets that
  the other is still held.
- The tray icon comes back after Explorer restarts when Switchy runs as
  administrator (Windows filtered out Explorer's message), is redrawn for the
  taskbar's DPI, and a repeated "taskbar created" message no longer makes it
  retry every 2 s forever.
- Handing over to the admin task can no longer re-install the old copy's hook
  for a moment (two hooks = two switches per press).
- A pop-up press queued just before unlock/resume can no longer leave Win held;
  the tray menu can't open a second time while an autostart change is running.
- Out of GDI resources at start: no icon instead of a crash.

### Changed
- The full-screen check (foreground hook + 1 s timer) only runs while its menu
  option is on: switched off, nothing wakes Switchy up.
- 20.5 KB instead of 21 KB (`-Oz`, the task XML template stored as 8-bit text,
  smaller manifest, no `GetModuleHandle`). Needs Windows 10 1607 or later.
- Live test: whole HKL, lost-release cases, a non-admin second copy, the frozen
  process is always resumed. CI checks that only KnownDLLs are imported and
  that ASLR/NX are on.

## 2.2.0 — 2026-10-21

### Changed
- Caps Lock now does exactly what your Windows language hotkey does (Alt+Shift by
  default), silently, by default. The Win+Space pop-up is an opt-in in the tray
  menu. Existing users who turned the pop-up on keep it.

### Added
- Full-screen games keep Caps Lock: while a full-screen app is in front (exclusive
  D3D, or a borderless window covering its monitor), Caps Lock, Shift+Caps Lock and
  Alt+Caps Lock reach the app untouched. On by default; tray menu "Leave Caps Lock
  to full-screen games". Checked outside the keyboard hook (on focus change and once
  a second); the hook only reads a flag. A maximized window or a full-screen video
  on another monitor does not count.

## 2.1.1 — 2026-10-21

### Security
- Keys injected by a less privileged process are ignored (they still reach
  applications as usual). Otherwise any normal program could make an elevated
  Switchy press Win, Alt+Shift or Caps Lock on its behalf, around UIPI.
  Idea from the ansromanov/Switchy fork.

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
