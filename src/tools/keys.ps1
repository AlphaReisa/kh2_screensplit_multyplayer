# Sends keys to KH2 window: .\keys.ps1 -keys "ENTER","DOWN" -delay 600
param([string]$keys, [int]$delay = 500)
$list = $keys -split "[ ,+]+" | Where-Object { $_ }
Add-Type @"
using System; using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int p);
}
"@
$vk = @{ ENTER=0x0D; ESC=0x1B; UP=0x26; DOWN=0x28; LEFT=0x25; RIGHT=0x27; SPACE=0x20; OEM3=0xC0; F1=0x70; F2=0x71; F3=0x72; SHIFT=0x10; F4=0x73; F5=0x74; F6=0x75; F7=0x76; F8=0x77; F9=0x78; F10=0x79; F11=0x7A; F12=0x7B }
$p = Get-Process -Name "KINGDOM HEARTS II FINAL MIX" | Select-Object -First 1
[K]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 300
"foreground ok: " + ([K]::GetForegroundWindow() -eq $p.MainWindowHandle)
foreach ($k in $list) {
  if ($k -match '^\+(\w+)$') { $m=$Matches[1]; $mc=$vk[$m]; [K]::keybd_event($mc, 0, 0, [UIntPtr]::Zero); $mod=$mc; continue }
  if ($k -match '^(\w+):(\d+)$') {   # hold KEY:ms
    $hc = if ($vk.ContainsKey($Matches[1])) { $vk[$Matches[1]] } else { [byte][char]$Matches[1].ToUpper() }
    $hs = [byte][K]::MapVirtualKey($hc, 0)
    [K]::keybd_event($hc, $hs, 8, [UIntPtr]::Zero); Start-Sleep -Milliseconds ([int]$Matches[2])
    [K]::keybd_event($hc, $hs, 10, [UIntPtr]::Zero); Start-Sleep -Milliseconds $delay; continue }
  if ($k -match '^wait(\d+)$') { Start-Sleep -Milliseconds ([int]$Matches[1]); continue }
  $c = if ($vk.ContainsKey($k)) { $vk[$k] } else { [byte][char]$k.ToUpper() }
  $s = [byte][K]::MapVirtualKey($c, 0)
  $ext = if ($c -ge 0x25 -and $c -le 0x28) { 1 } else { 0 }   # arrows are extended keys
  [K]::keybd_event($c, $s, (8 -bor $ext), [UIntPtr]::Zero); Start-Sleep -Milliseconds 100
  [K]::keybd_event($c, $s, (8 -bor 2 -bor $ext), [UIntPtr]::Zero)
  if ($mod) { [K]::keybd_event($mod, 0, 2, [UIntPtr]::Zero); $mod = $null }
  Start-Sleep -Milliseconds $delay
}
