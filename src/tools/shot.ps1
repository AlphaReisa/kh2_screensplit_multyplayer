# Captures the primary screen to F:\KH2-Coop\research\shots\<name>.png (downscaled to 1280 wide)
param([string]$name = "shot")
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class DPI { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[DPI]::SetProcessDPIAware() | Out-Null
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$w = 1280; $h = [int]($b.Height * $w / $b.Width)
$small = New-Object System.Drawing.Bitmap $bmp, $w, $h
$dir = "F:\KH2-Coop\research\shots"
New-Item -ItemType Directory -Force $dir | Out-Null
$small.Save("$dir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
"$dir\$name.png"
