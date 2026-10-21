# Live test of a running Switchy on this desktop (Windows PowerShell 5.1 or 7).
# Sends synthetic keys (dwExtraInfo = 0, so Switchy treats them as physical),
# checks the layout and Caps Lock state in a test window of its own kept in front
# (so a click elsewhere can't skew it), stuck-key recovery, and recovery after
# Windows silently drops the hook (process frozen > timeout). Don't type meanwhile.
param([switch]$SkipHookDrop, [string]$Exe = (Join-Path $PSScriptRoot '..\build\Switchy.exe'))
$ErrorActionPreference = 'Stop'
Add-Type -Language CSharp @'
using System; using System.Runtime.InteropServices;
public static class K {
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct IN { public uint type; public KI ki; public long pad; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, IN[] i, int cb);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint tid);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll")] public static extern short GetKeyState(int vk);
  [DllImport("ntdll.dll")] public static extern int NtSuspendProcess(IntPtr h);
  [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr h);
  public static void Key(ushort vk, bool up) { Key(vk, up, 0); }
  // extra = 'SWTY': Switchy takes the key for its own and doesn't see it (a "lost" key)
  public static void Key(ushort vk, bool up, long extra) {
    var i = new IN[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.flags = up ? 2u : 0u; i[0].ki.extra = new IntPtr(extra);
    SendInput(1, i, Marshal.SizeOf(typeof(IN)));
  }
  // the whole HKL: two layouts of one language differ only in the high word
  public static long Layout() { return GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero)).ToInt64(); }
}
'@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Language CSharp @'
using System; using System.Runtime.InteropServices;
public static class Fg { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
'@
$CAPS = 0x14; $LSHIFT = 0xA0; $LMENU = 0xA4; $LWIN = 0x5B; $ESC = 0x1B; $SWTY = 0x53575459
$fail = 0
# The test window: a normal one (it has a title bar, so it's not "full-screen").
# Its messages are pumped while waiting, so it applies layout changes. No system
# menu: Alt+Caps Lock reaches it as a lone Alt, which would start a modal menu loop.
$tw = New-Object System.Windows.Forms.Form
$tw.Text = 'Switchy live test - do not type'; $tw.ControlBox = $false; $tw.Width = 420; $tw.Height = 160; $tw.TopMost = $true; $tw.StartPosition = 'CenterScreen'
$tw.Show()
function Wait($ms) { $end = (Get-Date).AddMilliseconds($ms); do { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 10 } while ((Get-Date) -lt $end) }
function Front {   # an Alt tap lets us take the foreground; Esc leaves the menu mode it starts
  if ([K]::GetForegroundWindow() -eq $tw.Handle) { return }
  [K]::Key($LMENU, $false); [K]::Key($LMENU, $true); [Fg]::SetForegroundWindow($tw.Handle) | Out-Null
  Wait 100; [K]::Key($ESC, $false); [K]::Key($ESC, $true); Wait 200
}
function Check($ok, $what) { if ($ok) { "  ok   $what" } else { "  FAIL $what"; $script:fail++ } }
function Tap($vk) { [K]::Key($vk, $false); Wait 40; [K]::Key($vk, $true); Wait 350 }
function CapsOn { ([K]::GetKeyState($CAPS) -band 1) -ne 0 }
# erryox/Switchy#15: a Win key left "held" made arrows maximize/restore windows
function NoWin { Check (([K]::GetAsyncKeyState($LWIN) -band 0x8000) -eq 0) "Win key is not held" }

$p = Get-Process Switchy -ErrorAction Stop | Select-Object -First 1
$meElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $p.Path -and -not $meElevated) {
  # Switchy runs elevated: it ignores keys injected from a lower integrity level by design.
  "Switchy runs as administrator: run this test from an elevated shell."; exit 2
}
Front
"Switchy pid $($p.Id), test window layout {0:X}" -f [K]::Layout()

"1. Caps Lock switches the layout and back"
Front
$capsBefore = CapsOn
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS; $c = [K]::Layout()
Check ($a -ne $b -and $a -eq $c) ("layouts {0:X} -> {1:X} -> {2:X}" -f $a, $b, $c)
Check ((CapsOn) -eq $capsBefore) "Caps Lock state untouched"
NoWin

"2. Shift+Caps Lock toggles Caps Lock, layout stays"
Front
$l = [K]::Layout(); $before = CapsOn
[K]::Key($LSHIFT, $false); Wait 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Wait 200
Check ((CapsOn) -ne $before -and [K]::Layout() -eq $l) "Caps Lock toggled, layout same"
[K]::Key($LSHIFT, $false); Wait 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Wait 200
Check ((CapsOn) -eq $before) "toggled back"
NoWin

"3. Caps Lock then Shift (reverse order) toggles Caps Lock"
Front
$l = [K]::Layout(); $before = CapsOn
[K]::Key($CAPS, $false); Wait 40; [K]::Key($LSHIFT, $false); Wait 40
[K]::Key($LSHIFT, $true); Wait 40; [K]::Key($CAPS, $true); Wait 400
Check ((CapsOn) -ne $before -and [K]::Layout() -eq $l) "Caps Lock toggled, layout same"
[K]::Key($LSHIFT, $false); Wait 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Wait 200
NoWin

"4. Fast double Caps Lock never disables Switchy (original bug)"
Front
$a = [K]::Layout()
for ($i = 0; $i -lt 6; $i++) { [K]::Key($CAPS, $false); Wait 15; [K]::Key($CAPS, $true); Wait 120 }
Wait 500
Check ([K]::Layout() -eq $a) "6 fast presses = back to the same layout"
$b0 = [K]::Layout(); Tap $CAPS; $b1 = [K]::Layout(); Tap $CAPS
Check ($b0 -ne $b1) "still enabled afterwards"
NoWin

"5. Alt+Caps Lock disables (Caps Lock works as usual), Alt+Caps Lock enables"
Front
[K]::Key($LMENU, $false); Wait 40; Tap $CAPS; [K]::Key($LMENU, $true); Wait 200
$l = [K]::Layout(); $before = CapsOn; Tap $CAPS
Check ([K]::Layout() -eq $l -and (CapsOn) -ne $before) "disabled: Caps Lock toggles Caps, no switch"
Tap $CAPS
[K]::Key($LMENU, $false); Wait 40; Tap $CAPS; [K]::Key($LMENU, $true); Wait 200
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "re-enabled"
NoWin

"5b. Caps Lock then Alt (reverse order) disables, layout stays"
Front
$l = [K]::Layout()
[K]::Key($CAPS, $false); Wait 40; [K]::Key($LMENU, $false); Wait 40
[K]::Key($LMENU, $true); Wait 40; [K]::Key($CAPS, $true); Wait 400
Check ([K]::Layout() -eq $l) "layout same"
$before = CapsOn; Tap $CAPS
Check ((CapsOn) -ne $before -and [K]::Layout() -eq $l) "disabled: Caps Lock is a plain Caps Lock"
Tap $CAPS
[K]::Key($LMENU, $false); Wait 40; Tap $CAPS; [K]::Key($LMENU, $true); Wait 200
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "re-enabled with Alt+Caps Lock"
NoWin

"6. Lost Caps Lock release: held Win is released by the stale timer (Win is held only in pop-up mode)"
Front
[K]::Key($CAPS, $false); Wait 2600
Check (([K]::GetAsyncKeyState($LWIN) -band 0x8000) -eq 0) "Win is not stuck"
[K]::Key($CAPS, $true); Wait 300
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "works after recovery"

"6a. Lost Alt / Shift release (it went to an admin window): Caps Lock still switches"
Front
$a = [K]::Layout()
[K]::Key($LMENU, $false); Wait 40; [K]::Key($LMENU, $true, $SWTY); Wait 100
[K]::Key($ESC, $false); [K]::Key($ESC, $true); Wait 100   # the Alt tap started menu mode
Tap $CAPS; $b = [K]::Layout()
Check ($a -ne $b) "after a lost Alt release: switched, not disabled"
if ($a -ne $b) { Tap $CAPS }
$a = [K]::Layout(); $before = CapsOn
[K]::Key($LSHIFT, $false); Wait 40; [K]::Key($LSHIFT, $true, $SWTY); Wait 100
Tap $CAPS; $b = [K]::Layout()
Check ($a -ne $b -and (CapsOn) -eq $before) "after a lost Shift release: switched, Caps Lock untouched"
if ($a -ne $b) { Tap $CAPS }
NoWin

"6b. Full-screen (borderless) window in front: Caps Lock is left to it"
Front
$f = New-Object System.Windows.Forms.Form
$f.FormBorderStyle = 'None'; $f.StartPosition = 'Manual'; $f.TopMost = $true; $f.ShowInTaskbar = $false
$f.Bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $f.BackColor = 'Black'
$f.Show()
[K]::Key($LMENU, $false); [K]::Key($LMENU, $true)          # an Alt tap lets us take the foreground
[Fg]::SetForegroundWindow($f.Handle) | Out-Null
for ($i = 0; $i -lt 20; $i++) { [System.Windows.Forms.Application]::DoEvents(); Wait 100 }  # > 1 s re-check
if ([K]::GetForegroundWindow() -ne $f.Handle) {
  "  skip: could not bring the test window to the front"
} else {
  $l = [K]::Layout(); $before = CapsOn
  Tap $CAPS; [System.Windows.Forms.Application]::DoEvents()
  Check ([K]::Layout() -eq $l -and (CapsOn) -ne $before) "full-screen: plain Caps Lock, no switch"
  Tap $CAPS; [System.Windows.Forms.Application]::DoEvents()
}
$f.Close(); $f.Dispose()
Front
Wait 1500
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "after closing it Caps Lock switches again"
NoWin

"7. Second instance exits silently"
Front
Start-Process $Exe; Wait 2000   # same rights as this shell
Check (@(Get-Process Switchy).Count -eq 1) "still one process"
if ($meElevated) {
  Start-Process explorer.exe "`"$((Resolve-Path $Exe).Path)`""; Wait 3000   # Explorer starts it without admin rights
  Check (@(Get-Process Switchy).Count -eq 1) "still one process (a normal copy next to the admin one)"
}

if (-not $SkipHookDrop) {
  "8. Windows drops the hook (process frozen past LowLevelHooksTimeout), Switchy re-installs it"
  Front
  $h = (Get-Process -Id $p.Id).Handle
  [K]::NtSuspendProcess($h) | Out-Null
  try { for ($i = 0; $i -lt 8; $i++) { Tap $CAPS } }     # Windows waits for the hook, gives up, removes it
  finally { [K]::NtResumeProcess($h) | Out-Null }         # never leave Switchy frozen
  Wait 500
  $a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); if ($a -ne $b) { Tap $CAPS }
  "  info right after the drop: switch {0}" -f $(if ($a -ne $b) { 'worked (hook survived)' } else { 'did not work (hook dropped)' })
  $deadline = (Get-Date).AddSeconds(30); $ok = $false
  while ((Get-Date) -lt $deadline -and -not $ok) {
    Wait 3000; Front; $a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout()
    if ($a -ne $b) { $ok = $true; Tap $CAPS }
  }
  Check $ok "works again within 30 s"
}

$ws = Get-Process -Id $p.Id
if ($ws.Path) { "process: WS {0:N1} MB, CPU {1:N2} s, threads {2}" -f ($ws.WorkingSet64 / 1MB), $ws.CPU, $ws.Threads.Count }
$tw.Close()
if ($fail) { "FAILED: $fail" ; exit 1 } else { "ALL PASSED" }
