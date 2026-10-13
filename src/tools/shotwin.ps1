# Captures the KH2 window (even if covered) via PrintWindow(PW_RENDERFULLCONTENT) -> research\shots\<name>.png
param([string]$name = "win")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  public struct RECT { public int L, T, R, B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$p = Get-Process -Name "KINGDOM HEARTS II FINAL MIX" -ErrorAction Stop | Select-Object -First 1
$h = $p.MainWindowHandle
$r = New-Object W+RECT
[W]::GetClientRect($h, [ref]$r) | Out-Null
$wd = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $wd, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[W]::PrintWindow($h, $hdc, 3) | Out-Null   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
# crop centered 16:9 game area (ultrawide window is pillarboxed)
$gw = [Math]::Min($wd, [int]($ht * 16 / 9)); $gx = [int](($wd - $gw) / 2)
$crop = $bmp.Clone((New-Object System.Drawing.Rectangle $gx, 0, $gw, $ht), $bmp.PixelFormat)
$w = 1280; $hh = [int]($ht * $w / $gw)
$small = New-Object System.Drawing.Bitmap $crop, $w, $hh
$dir = "F:\KH2-Coop\research\shots"
New-Item -ItemType Directory -Force $dir | Out-Null
$small.Save("$dir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
"$dir\$name.png ($wd x $ht)"
