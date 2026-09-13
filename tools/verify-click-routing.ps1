param(
    # Defaults to the Release build next to this script's repo. Run it from a
    # scratch directory: the captures and crops land in the current directory.
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\bin\Release\webview2_dcomp1.exe"),
    [string]$Mode = "Composition",
    [int]$WaitMs = 8000
)

# Two checks, in whichever hosting mode is asked for:
#
#   1. Clicking the empty middle of the cut-out pauses the GIF, clicking again
#      resumes it. Measured as the fraction of the circle that changes between
#      two captures 0.9s apart: playing scores high, paused scores 0%.
#   2. Clicking the page's buttons still counts. The buttons are HTML, so they
#      have no HWND to aim at - their screen positions are computed from the CSS
#      box model, and the counters are read back off rt_card_final.png
#      afterwards (they are page text, so pixels are the only way to see them
#      from outside the process).

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class RtWnd {
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern int MapWindowPoints(IntPtr from, IntPtr to, ref POINT p, uint n);
    [DllImport("user32.dll")] public static extern IntPtr SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

[RtWnd]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
Get-Process -Name "webview2_dcomp1" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item "$env:TEMP\webview2_dcomp1_debug.log" -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds $WaitMs

$h = [RtWnd]::FindWindowW("webview2_dcomp1.MainWnd", "WebView2 DComp three-layer demo")
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; Stop-Process -Id $p.Id -Force; exit 1 }
[void][RtWnd]::SetWindowPos($h, [IntPtr](-1), 40, 40, 1100, 800, 0x0040)
[RtWnd]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1500

$script:mainWnd = $h

function ClassOf([IntPtr]$w) {
    $s = New-Object System.Text.StringBuilder 128
    [RtWnd]::GetClassNameW($w, $s, 128) | Out-Null
    return $s.ToString()
}
function FindChildByClass([string]$cls) {
    $script:found = [IntPtr]::Zero
    $cb = [RtWnd+EnumProc]{
        param($c, $lp)
        if ((ClassOf $c) -eq $cls) { $script:found = $c }
        return $true
    }
    [RtWnd]::EnumChildWindows($h, $cb, [IntPtr]::Zero) | Out-Null
    return $script:found
}
function ButtonText() {
    $b = FindChildByClass "Button"
    if ($b -eq [IntPtr]::Zero) { return "(none)" }
    $s = New-Object System.Text.StringBuilder 300
    [RtWnd]::GetWindowTextW($b, $s, 300) | Out-Null
    return $s.ToString()
}

function ClickScreen([int]$sx, [int]$sy, [string]$what) {
    # Check the point resolves inside the app before firing, so a click lost to
    # another window is reported instead of being read as a broken feature.
    $pt = New-Object RtWnd+POINT; $pt.X = $sx; $pt.Y = $sy
    $owner = [RtWnd]::WindowFromPoint($pt)
    $root = $owner
    for ($i = 0; $i -lt 8 -and $root -ne [IntPtr]::Zero; $i++) {
        $par = [RtWnd]::GetParent($root)
        if ($par -eq [IntPtr]::Zero) { break }
        $root = $par
    }
    if ($root -ne $script:mainWnd) {
        Write-Output ("CLICK-SKIPPED {0} at {1},{2}: owner {3} is not the app" -f $what, $sx, $sy, (ClassOf $owner))
        return
    }
    [RtWnd]::SetCursorPos($sx, $sy) | Out-Null
    Start-Sleep -Milliseconds 150
    [RtWnd]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 70
    [RtWnd]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function EnsureMode([string]$want, [int]$tries = 3) {
    for ($i = 1; $i -le $tries; $i++) {
        if ((ButtonText) -like "*$want*") { return $true }
        $b = FindChildByClass "Button"
        $r = New-Object RtWnd+RECT
        [RtWnd]::GetWindowRect($b, [ref]$r) | Out-Null
        ClickScreen ([int](($r.Left+$r.Right)/2)) ([int](($r.Top+$r.Bottom)/2)) "mode-button"
        Start-Sleep -Milliseconds 3000
    }
    return ((ButtonText) -like "*$want*")
}

# --- geometry ------------------------------------------------------------
$wr = New-Object RtWnd+RECT; [RtWnd]::GetWindowRect($h, [ref]$wr) | Out-Null
$cr = New-Object RtWnd+RECT; [RtWnd]::GetClientRect($h, [ref]$cr) | Out-Null
$co = New-Object RtWnd+POINT
[void][RtWnd]::MapWindowPoints($h, [IntPtr]::Zero, [ref]$co, 1)
$script:dx = $co.X - $wr.Left
$script:dy = $co.Y - $wr.Top
$script:ox = $co.X
$script:oy = $co.Y
$script:cw = $cr.Right - $cr.Left
$script:ch = $cr.Bottom - $cr.Top
$script:pageH = $script:ch - 44
Write-Output ("CLIENT={0}x{1} ORIGIN={2},{3} pageH={4}" -f $script:cw, $script:ch, $co.X, $co.Y, $script:pageH)

function RefreshRect() {
    $r = New-Object RtWnd+RECT
    [RtWnd]::GetWindowRect($h, [ref]$r) | Out-Null
    $script:wx = $r.Left; $script:wy = $r.Top
    $script:ww = $r.Right - $r.Left; $script:wh = $r.Bottom - $r.Top
}
function Snap([string]$name) {
    RefreshRect
    $bmp = New-Object System.Drawing.Bitmap $script:ww, $script:wh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($script:wx, $script:wy, 0, 0, (New-Object System.Drawing.Size($script:ww, $script:wh)))
    $g.Dispose()
    $path = "rt_$name.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return $path
}
function CircleChangedPct([string]$a, [string]$b) {
    $ba = [System.Drawing.Bitmap]::FromFile($a)
    $bb = [System.Drawing.Bitmap]::FromFile($b)
    $cx = [int]($script:cw / 2); $cy = [int]($script:pageH / 2); $rad = 260
    $changed = 0; $total = 0
    for ($y = $cy - $rad; $y -lt $cy + $rad; $y += 8) {
        for ($x = $cx - $rad; $x -lt $cx + $rad; $x += 8) {
            $ddx = $x - $cx; $ddy = $y - $cy
            if (($ddx*$ddx + $ddy*$ddy) -gt ($rad*$rad)) { continue }
            $ca = $ba.GetPixel($x + $script:dx, $y + $script:dy)
            $cb = $bb.GetPixel($x + $script:dx, $y + $script:dy)
            $d = [Math]::Abs($ca.R-$cb.R) + [Math]::Abs($ca.G-$cb.G) + [Math]::Abs($ca.B-$cb.B)
            if ($d -gt 24) { $changed++ }
            $total++
        }
    }
    $ba.Dispose(); $bb.Dispose()
    if ($total -eq 0) { return -1 }
    return [int](100.0 * $changed / $total)
}
function ReadAnim([string]$tag) {
    $s1 = Snap "$($tag)_a"
    Start-Sleep -Milliseconds 900
    $s2 = Snap "$($tag)_b"
    Write-Output ("{0}: circle anim={1}%" -f $tag, (CircleChangedPct $s1 $s2))
}

# Screen points of the three buttons in the .card-actions row, derived from the
# CSS box model rather than found by scanning pixels.
#
# Scanning for the button gradient was tried first and is not reliable here: the
# gap between button A and button B sits on the cut-out's rim, so depending on
# which GIF frame is on screen at capture time a few of its blue pixels bridge
# the gap and merge two buttons into one cluster whose centroid lands in the gap
# - i.e. between the buttons, on nothing. The layout is fixed by CSS, so compute
# it instead; the click counting is verified independently by reading the
# counter text back off a screenshot afterwards.
#
#   style.css  .card-actions { top:24px; right:24px; width:400px }
#              .card         { padding:18px 20px }
#              .btn-row      { display:flex; gap:8px; margin-top:8px }
#              .btn          { flex:1 1 0; padding:10px 14px; font-size:14px }
# Three equal buttons across 400 - 2*20 = 360px of content with two 8px gaps.
function ComputeButtonPoints() {
    $contentL = $script:cw - 24 - 400 + 20
    $contentW = 400 - 40
    $bw = ($contentW - 2 * 8) / 3
    # Vertical: card top 24 + padding 18, then the h2 (~19 + 10 margin), the two
    # wrapped hint lines (2*19 + 8 margin) and the row's 8px top margin put the
    # buttons' top edge at ~126; a button is ~40px tall, so the centre is ~146.
    $btnCy = 146
    $pts = @()
    for ($i = 0; $i -lt 3; $i++) {
        $cx = $contentL + ($bw + 8) * $i + $bw / 2
        $pts += ,@([int]($script:ox + $cx), [int]($script:oy + $btnCy))
    }
    return $pts
}

# --- run -----------------------------------------------------------------
$want = if ($Mode -eq "Windowed") { "Windowed (HWND)" } else { "Composition (DComp)" }
if (-not (EnsureMode $want)) { Write-Output "MODE-SWITCH-FAILED -> $want" }
Start-Sleep -Milliseconds 3000
Write-Output ("MODE='{0}'" -f (ButtonText))

Snap "locate" | Out-Null
$btns = @(ComputeButtonPoints)
Write-Output ("button points: {0}" -f $btns.Count)
foreach ($b in $btns) { Write-Output ("  button at {0},{1}" -f $b[0], $b[1]) }

$circleX = $co.X + [int]($script:cw / 2)
$circleY = $co.Y + [int]($script:pageH / 2)
Write-Output ("cut-out centre at {0},{1}" -f $circleX, $circleY)

ReadAnim "1_playing"

Write-Output "--- click 1: cut-out centre (expect pause) ---"
ClickScreen $circleX $circleY "cut-out"
Start-Sleep -Milliseconds 400
ReadAnim "2_after_first_click"

Write-Output "--- click 2: cut-out centre (expect resume) ---"
ClickScreen $circleX $circleY "cut-out"
Start-Sleep -Milliseconds 400
ReadAnim "3_after_second_click"

if ($btns.Count -ge 3) {
    Write-Output "--- buttons: A x3, B x1, C x2 (expect A:3 B:1 C:2 Total:6) ---"
    1..3 | ForEach-Object { ClickScreen $btns[0][0] $btns[0][1] "button A" }
    ClickScreen $btns[1][0] $btns[1][1] "button B"
    1..2 | ForEach-Object { ClickScreen $btns[2][0] $btns[2][1] "button C" }
    Start-Sleep -Milliseconds 400
}

Write-Output "--- click 3: cut-out centre (expect pause) ---"
ClickScreen $circleX $circleY "cut-out"
Start-Sleep -Milliseconds 400
ReadAnim "4_after_third_click"

Snap "final" | Out-Null

# Crop the actions card out of the final capture. The counters are page text, so
# the only way to read them from outside the process is to look at the pixels -
# this is the artefact the "buttons counted the clicks" claim is checked against.
function CropCard([string]$name) {
    $src = [System.Drawing.Bitmap]::FromFile((Resolve-Path "rt_$name.png"))
    $x = $script:dx + $script:cw - 430
    $y = $script:dy + 20
    $rect = New-Object System.Drawing.Rectangle $x, $y, 420, 230
    $crop = $src.Clone($rect, $src.PixelFormat)
    $out = "rt_card_$name.png"
    $crop.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop.Dispose(); $src.Dispose()
    Write-Output ("card crop: {0}" -f $out)
}
CropCard "final"

Stop-Process -Id $p.Id -Force
Write-Output "DONE"
