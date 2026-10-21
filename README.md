# Switchy

Switch keyboard layout with **Caps Lock** on Windows 10/11. One 19 KB executable,
no installer, no runtime, no dependencies beyond Windows itself.

*Maintained fork of [erryox/Switchy](https://github.com/erryox/Switchy) (archived in 2024).
Русская версия ниже.*

| Keys | Action |
|---|---|
| **Caps Lock** | switch to the next layout |
| **Shift+Caps Lock** (or Caps Lock, then Shift) | toggle the real Caps Lock |
| **Alt+Caps Lock** (or Caps Lock, then Alt) | turn Switchy off / on (the tray icon turns grey) |

## Install

1. Download `Switchy.exe` from [Releases](../../releases/latest) and put it anywhere.
2. Run it. A blue **S** appears in the tray.
3. Right-click the icon and pick one autostart option:
   * **Start with Windows**: runs at sign-in as a normal app.
   * **Start with Windows as administrator**: also works inside windows that run
     as administrator (admin terminals, installers). The exe is copied to
     `C:\Program Files\Switchy` and a logon task is created (one UAC prompt).
     Windows does not let a normal app see keys typed into elevated windows.

Coming from the original Switchy: just run the new exe. It uses the same
single-instance name, removes the old `shell:startup` copy when you enable an
autostart option, and keeps honouring the `nopopup` argument.

## Tray menu

* **Enabled**: same as Alt+Caps Lock. Double-click the icon to toggle.
* **Show layout pop-up**: switch through the Win+Space flyout (default, as in
  the original) or silently.
* **Start with Windows** / **…as administrator**: see above. Only one is active.

## What changed compared to the original

* **Recovers if Windows drops the keyboard hook.** Windows silently removes a
  low-level hook that answers too slowly, and the app keeps running "dead".
  Switchy now re-installs the hook every 20 seconds and after unlock/resume, and
  works to never be slow in the first place:
  * the hook only updates a state machine and posts a message; key injection
    happens outside the hook;
  * the working set has a hard minimum, so the hook code is never paged out
    under memory pressure (a page fault from a busy disk is a classic way to
    miss the deadline);
  * above-normal priority, Windows 11 EcoQoS throttling off.
* **Fast double Caps Lock no longer disables Switchy.** The original switched
  layouts by injecting Alt+Shift and could read its own injected Alt as a real
  one on the next press, toggling itself off (likely erryox/Switchy#14). Only
  physical Alt counts now, and all injected keys go out in one `SendInput`
  call, so a real key cannot land in the middle.
* **Uses your Windows language hotkey**: Alt+Shift, Ctrl+Shift, grave accent, or
  Win+Space when none is set. The original always sent Alt+Shift.
* **Tray icon** shows whether Switchy is on; menu for settings and autostart.
* **Works in admin windows** with the built-in elevated autostart.
* **Stuck-key protection**: if a Caps Lock release is lost (lock screen, UAC
  prompt), the held Win key of the pop-up mode is released after 1.5 s.
* **Caps Lock, then Shift** in pop-up mode toggles Caps Lock and leaves the
  layout as it was.
* A second copy exits silently (also when the running one is elevated) instead
  of showing an error box.
* Other programs can't close it with WM_CLOSE; only the tray menu exits. If it
  ever goes away, `HKCU\Software\Switchy\LastExit` says why.
* Built without the C runtime: 19 KB, imports only `kernel32`, `user32`,
  `shell32`, `gdi32`, `advapi32`, `wtsapi32`. DPI-aware tray icon.

## Upstream issues

| erryox/Switchy | Status in this fork |
|---|---|
| [#14](https://github.com/erryox/Switchy/issues/14) Shift+Caps Lock randomly disables Switchy | fixed: only a physical Alt counts, own injected keys are tagged |
| [#15](https://github.com/erryox/Switchy/issues/15) arrows maximize windows (a stuck Win key) | fixed: one Win press per Caps press, released on any exit path and after 1.5 s at worst |
| [#16](https://github.com/erryox/Switchy/issues/16) `nopopup` does nothing | likely fixed: the Windows language hotkey is read instead of always sending Alt+Shift |
| [#11](https://github.com/erryox/Switchy/issues/11) Ctrl+Shift hotkey not supported | fixed |
| [#13](https://github.com/erryox/Switchy/issues/13) Shift+Caps Lock also switches layout | fixed (covered by tests) |
| [#5](https://github.com/erryox/Switchy/issues/5), [#2](https://github.com/erryox/Switchy/issues/2) Caps Lock can't be turned off with "press Shift to turn off" | fixed (Shift fallback) |
| [#8](https://github.com/erryox/Switchy/issues/8) can't find the process, no way to exit | tray icon with Exit |
| [#2](https://github.com/erryox/Switchy/issues/2) "different input method for each app window" | not verified |
| [#6](https://github.com/erryox/Switchy/issues/6) cycle only some layouts (e.g. EN/RU out of EN/RU/UA) | not implemented yet |

## Build

Any of:

```sh
CC=x86_64-w64-mingw32-clang ./build.sh   # LLVM-MinGW (release builds use 20261006)
./build.sh                               # MinGW-w64 GCC
msbuild Switchy.sln /p:Configuration=Release /p:Platform=x64   # Visual Studio 2022
```

Tests:

```sh
./build.sh test                             # key state machine, any OS, plain C
pwsh tests/live_test.ps1                    # against a running Switchy on your desktop
```

`tests/live_test.ps1` sends synthetic keys (it will flick your layout a few times)
and checks layout switching, Shift+Caps Lock in both orders, Alt+Caps Lock,
fast double presses, stuck-key recovery and the single-instance guard.

## License

MIT, same as the original. See [LICENSE](LICENSE).

---

# Switchy (по-русски)

Переключение раскладки клавишей **Caps Lock** в Windows 10/11. Один файл на
19 КБ, без установщика и рантаймов. Поддерживаемый форк заброшенного в 2024 году
[erryox/Switchy](https://github.com/erryox/Switchy).

* **Caps Lock** — следующая раскладка.
* **Shift+Caps Lock** (или Caps Lock, затем Shift) — настоящий Caps Lock.
* **Alt+Caps Lock** — выключить/включить Switchy (значок в трее станет серым).

**Установка.** Скачайте `Switchy.exe` из [релизов](../../releases/latest),
запустите и в меню значка в трее выберите автозапуск:
* «Запускать вместе с Windows» — обычный режим;
* «Запускать от администратора» — тогда Caps Lock работает и в окнах,
  запущенных от администратора. Один запрос UAC: файл копируется в
  `C:\Program Files\Switchy`, создаётся задача планировщика.

**Что исправлено по сравнению с оригиналом:**
* Windows молча снимает «медленный» перехват клавиатуры, и программа работает
  вхолостую. Теперь перехват восстанавливается каждые 20 секунд и после
  разблокировки или сна. Сам он отвечает мгновенно: код закреплён в памяти,
  повышен приоритет, отключён EcoQoS.
* Быстрое двойное нажатие Caps Lock больше не выключает программу.
* Используется ваше сочетание для смены языка из настроек Windows.
* Значок в трее, меню настроек, защита от залипшей клавиши Win, тихий выход
  второй копии.
