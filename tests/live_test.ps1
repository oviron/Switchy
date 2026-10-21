# Live test of a running Switchy on this desktop (Windows PowerShell 5.1 or 7).
# Sends synthetic keys (dwExtraInfo = 0, so Switchy treats them as physical),
# checks the foreground window's layout, Caps Lock state, stuck-key recovery,
# and recovery after Windows silently drops the hook (process frozen > timeout).
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
  public static void Key(ushort vk, bool up) {
    var i = new IN[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.flags = up ? 2u : 0u;
    SendInput(1, i, Marshal.SizeOf(typeof(IN)));
  }
  public static long Layout() { return GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero)).ToInt64() & 0xFFFF; }
}
'@
$CAPS = 0x14; $LSHIFT = 0xA0; $LMENU = 0xA4; $LWIN = 0x5B
$fail = 0
function Check($ok, $what) { if ($ok) { "  ok   $what" } else { "  FAIL $what"; $script:fail++ } }
function Tap($vk) { [K]::Key($vk, $false); Start-Sleep -Milliseconds 40; [K]::Key($vk, $true); Start-Sleep -Milliseconds 350 }
function CapsOn { ([K]::GetKeyState($CAPS) -band 1) -ne 0 }

$p = Get-Process Switchy -ErrorAction Stop | Select-Object -First 1
"Switchy pid $($p.Id), foreground layout {0:X4}" -f [K]::Layout()

"1. Caps Lock switches the layout and back"
$capsBefore = CapsOn
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS; $c = [K]::Layout()
Check ($a -ne $b -and $a -eq $c) ("layouts {0:X4} -> {1:X4} -> {2:X4}" -f $a, $b, $c)
Check ((CapsOn) -eq $capsBefore) "Caps Lock state untouched"

"2. Shift+Caps Lock toggles Caps Lock, layout stays"
$l = [K]::Layout(); $before = CapsOn
[K]::Key($LSHIFT, $false); Start-Sleep -Milliseconds 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Start-Sleep -Milliseconds 200
Check ((CapsOn) -ne $before -and [K]::Layout() -eq $l) "Caps Lock toggled, layout same"
[K]::Key($LSHIFT, $false); Start-Sleep -Milliseconds 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Start-Sleep -Milliseconds 200
Check ((CapsOn) -eq $before) "toggled back"

"3. Caps Lock then Shift (reverse order) toggles Caps Lock"
$l = [K]::Layout(); $before = CapsOn
[K]::Key($CAPS, $false); Start-Sleep -Milliseconds 40; [K]::Key($LSHIFT, $false); Start-Sleep -Milliseconds 40
[K]::Key($LSHIFT, $true); Start-Sleep -Milliseconds 40; [K]::Key($CAPS, $true); Start-Sleep -Milliseconds 400
Check ((CapsOn) -ne $before -and [K]::Layout() -eq $l) "Caps Lock toggled, layout same"
[K]::Key($LSHIFT, $false); Start-Sleep -Milliseconds 40; Tap $CAPS; [K]::Key($LSHIFT, $true); Start-Sleep -Milliseconds 200

"4. Fast double Caps Lock never disables Switchy (original bug)"
$a = [K]::Layout()
for ($i = 0; $i -lt 6; $i++) { [K]::Key($CAPS, $false); Start-Sleep -Milliseconds 15; [K]::Key($CAPS, $true); Start-Sleep -Milliseconds 120 }
Start-Sleep -Milliseconds 500
Check ([K]::Layout() -eq $a) "6 fast presses = back to the same layout"
$b0 = [K]::Layout(); Tap $CAPS; $b1 = [K]::Layout(); Tap $CAPS
Check ($b0 -ne $b1) "still enabled afterwards"

"5. Alt+Caps Lock disables (Caps Lock works as usual), Alt+Caps Lock enables"
[K]::Key($LMENU, $false); Start-Sleep -Milliseconds 40; Tap $CAPS; [K]::Key($LMENU, $true); Start-Sleep -Milliseconds 200
$l = [K]::Layout(); $before = CapsOn; Tap $CAPS
Check ([K]::Layout() -eq $l -and (CapsOn) -ne $before) "disabled: Caps Lock toggles Caps, no switch"
Tap $CAPS
[K]::Key($LMENU, $false); Start-Sleep -Milliseconds 40; Tap $CAPS; [K]::Key($LMENU, $true); Start-Sleep -Milliseconds 200
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "re-enabled"

"6. Lost Caps Lock release: held Win is released by the stale timer"
[K]::Key($CAPS, $false); Start-Sleep -Milliseconds 2600
Check (([K]::GetAsyncKeyState($LWIN) -band 0x8000) -eq 0) "Win is not stuck"
[K]::Key($CAPS, $true); Start-Sleep -Milliseconds 300
$a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); Tap $CAPS
Check ($a -ne $b) "works after recovery"

"7. Second instance exits silently"
Start-Process $Exe; Start-Sleep 2   # a normal (non-elevated) copy, even if the running one is elevated
Check (@(Get-Process Switchy).Count -eq 1) "still one process"

if (-not $SkipHookDrop) {
  "8. Windows drops the hook (process frozen past LowLevelHooksTimeout), Switchy re-installs it"
  $h = (Get-Process -Id $p.Id).Handle
  [K]::NtSuspendProcess($h) | Out-Null
  for ($i = 0; $i -lt 8; $i++) { Tap $CAPS }            # Windows waits for the hook, gives up, removes it
  [K]::NtResumeProcess($h) | Out-Null
  Start-Sleep -Milliseconds 500
  $a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout(); if ($a -ne $b) { Tap $CAPS }
  "  info right after the drop: switch {0}" -f $(if ($a -ne $b) { 'worked (hook survived)' } else { 'did not work (hook dropped)' })
  $deadline = (Get-Date).AddSeconds(30); $ok = $false
  while ((Get-Date) -lt $deadline -and -not $ok) {
    Start-Sleep 3; $a = [K]::Layout(); Tap $CAPS; $b = [K]::Layout()
    if ($a -ne $b) { $ok = $true; Tap $CAPS }
  }
  Check $ok "works again within 30 s"
}

$ws = Get-Process -Id $p.Id
if ($ws.Path) { "process: WS {0:N1} MB, CPU {1:N2} s, threads {2}" -f ($ws.WorkingSet64 / 1MB), $ws.CPU, $ws.Threads.Count }
if ($fail) { "FAILED: $fail" ; exit 1 } else { "ALL PASSED" }
