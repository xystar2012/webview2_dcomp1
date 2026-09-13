# Records docs/demo.gif: drives the app through a scripted run and captures the
# client area frame by frame. Run it from a scratch directory - the frames land
# in .\demo-frames (and *.png is git-ignored).
#
#   powershell -File tools\record-demo.ps1
#   python tools\build-demo-gif.py demo-frames docs\demo.gif
#
# The clicks are synthetic, and a synthetic click is not reliable: it needs a
# processed move at the new position first, and Chromium drops the click if the
# down/up pair lands too tight behind it. Rather than hope, this measures the
# GIF's actual play state from the captured pixels after every click and clicks
# again until it matches - see EnsurePlaying(). It also re-reads the client
# origin per frame, because MainWnd is created with CW_USEDEFAULT and can shift
# itself; a fixed capture origin silently records whatever is behind it.
param(
    [string]$Exe    = (Join-Path $PSScriptRoot "..\build\bin\Release\webview2_dcomp1.exe"),
    [string]$OutDir = (Join-Path (Get-Location) "demo-frames"),
    [int]$Fps       = 10,
    # 1 = grab one frame after load and report the state, then exit. Use it to
    # check the window is where the capture thinks it is before a full run.
    [int]$Probe     = 0
)

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class RecWnd {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern IntPtr SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

[RecWnd]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

Get-Process -Name "webview2_dcomp1" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600

if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$p = Start-Process -FilePath $Exe -PassThru

# Same placement the verify script uses, so its measured coordinates apply.
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 40 -and $hwnd -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $hwnd = [RecWnd]::FindWindowW("webview2_dcomp1.MainWnd", "WebView2 DComp three-layer demo")
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; Stop-Process -Id $p.Id -Force; exit 1 }

[RecWnd]::SetWindowPos($hwnd, [IntPtr](-1), 40, 40, 1100, 800, 0x0040) | Out-Null
[RecWnd]::ShowWindow($hwnd, 5) | Out-Null
[RecWnd]::SetForegroundWindow($hwnd) | Out-Null

# MainWnd is created with CW_USEDEFAULT, so Windows cascade-places it and an
# early SetWindowPos can be overwritten by the app's own show. Let the window
# settle, then pin it - and keep it topmost so nothing can occlude the capture.
function PinWindow() {
    [RecWnd]::SetWindowPos($hwnd, [IntPtr](-1), 40, 40, 1100, 800, 0x0040) | Out-Null
    [RecWnd]::SetForegroundWindow($hwnd) | Out-Null
}
Start-Sleep -Milliseconds 2000
PinWindow
Start-Sleep -Milliseconds 300

$cr = New-Object RecWnd+RECT; [RecWnd]::GetClientRect($hwnd, [ref]$cr) | Out-Null
$cw = $cr.Right - $cr.Left
$ch = $cr.Bottom - $cr.Top

# The client origin is re-read rather than captured once: the app can still
# shift itself, and a stale origin silently captures whatever is behind it.
$script:ox = 0; $script:oy = 0
function RefreshOrigin() {
    $o = New-Object RecWnd+POINT; $o.X = 0; $o.Y = 0
    [RecWnd]::ClientToScreen($hwnd, [ref]$o) | Out-Null
    $script:ox = $o.X; $script:oy = $o.Y
}
RefreshOrigin
Write-Output ("CLIENT={0}x{1} ORIGIN={2},{3}" -f $cw, $ch, $script:ox, $script:oy)

# Client coordinates. MainWnd re-centres itself after a SetWindowPos, so the
# window's screen position is not ours to fix - everything below is derived from
# the measured client origin at click time.
#   buttons: the actions card's row, 400px card at right:24 -> centres measured
#            at 731/854/977, row baseline y=146
#   cut-out: the disc is centred in the page area, which is 1078x700
$btnA = @(731, 146)
$btnB = @(854, 146)
$hole = @(539, 350)

# The cut-out disc, in client coordinates: centre and a radius that stays well
# inside the 324px hole so the rim's antialiasing cannot pollute the sample.
$discX = 539; $discY = 350; $discR = 140

function Grab([int]$x, [int]$y, [int]$w, [int]$h) {
    $b = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    return $b
}

# Fraction of sampled pixels inside the disc that differ between two grabs.
# Tens of percent while the GIF plays, exactly 0 while it is paused.
function DiscDiff() {
    RefreshOrigin
    $x = $script:ox + $discX - $discR
    $y = $script:oy + $discY - $discR
    $d = $discR * 2
    $a = Grab $x $y $d $d
    Start-Sleep -Milliseconds 380
    $b = Grab $x $y $d $d
    $changed = 0; $total = 0
    for ($py = 6; $py -lt $d; $py += 10) {
        for ($px = 6; $px -lt $d; $px += 10) {
            $dx = $px - $discR; $dy = $py - $discR
            if (($dx * $dx + $dy * $dy) -gt ($discR * $discR)) { continue }
            $total++
            $ca = $a.GetPixel($px, $py); $cb = $b.GetPixel($px, $py)
            if (([math]::Abs($ca.R - $cb.R) + [math]::Abs($ca.G - $cb.G) + [math]::Abs($ca.B - $cb.B)) -gt 24) { $changed++ }
        }
    }
    $a.Dispose(); $b.Dispose()
    if ($total -eq 0) { return 0.0 }
    return $changed / $total
}

$script:frame = 0
$intervalMs = [int](1000.0 / $Fps)

function CaptureFor([double]$sec) {
    $n = [int]($sec * $Fps)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $n; $i++) {
        RefreshOrigin
        $b = Grab $script:ox $script:oy $cw $ch
        $b.Save((Join-Path $OutDir ("f{0:d4}.png" -f $script:frame)), [System.Drawing.Imaging.ImageFormat]::Png)
        $b.Dispose()
        $script:frame++
        $wait = ($i + 1) * $intervalMs - $sw.Elapsed.TotalMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
    }
}

# A synthetic click needs a processed move at the new position first, and
# Chromium drops the click if the down/up pair arrives too tight behind it -
# which is what silently ate a cut-out click in an earlier take. So: move,
# settle, and hold the button long enough to be seen as a click.
# Takes client coordinates; the caller supplies the live client origin.
function ClickAt($pt) {
    [RecWnd]::SetForegroundWindow($hwnd) | Out-Null
    RefreshOrigin
    $sx = $script:ox + $pt[0]
    $sy = $script:oy + $pt[1]
    [RecWnd]::SetCursorPos($sx, $sy) | Out-Null
    Start-Sleep -Milliseconds 120
    [RecWnd]::SetCursorPos($sx + 1, $sy) | Out-Null   # force a real WM_MOUSEMOVE
    Start-Sleep -Milliseconds 120
    [RecWnd]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 110
    [RecWnd]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 180
}

$script:tries = 0
function EnsurePlaying([bool]$wantPlaying, $pt, [string]$what, [int]$maxTries = 4) {
    for ($t = 1; $t -le $maxTries; $t++) {
        $diff = DiscDiff
        $playing = $diff -gt 0.02
        if ($playing -eq $wantPlaying) {
            if ($t -gt 1) { Write-Output ("  {0}: settled after {1} clicks (diff={2:P0})" -f $what, ($t - 1), $diff) }
            return
        }
        Write-Output ("  {0}: diff={1:P0}, want {2} - clicking (try {3})" -f $what, $diff, $(if ($wantPlaying) { "playing" } else { "paused" }), $t)
        ClickAt $pt
        $script:tries++
    }
    Write-Output ("  {0}: GAVE UP" -f $what)
}

# Which mode is live, read from the window tree: in windowed mode VideoWnd's
# parent is Chromium's presenting widget, not BrowserWnd.
function ModeIsWindowed() {
    $script:video = [IntPtr]::Zero
    $cb = [RecWnd+EnumProc]{
        param($c, $lp)
        $s = New-Object System.Text.StringBuilder 128
        [RecWnd]::GetClassNameW($c, $s, 128) | Out-Null
        if ($s.ToString() -eq "webview2_dcomp1.VideoWnd") { $script:video = $c }
        return $true
    }
    [RecWnd]::EnumChildWindows($hwnd, $cb, [IntPtr]::Zero) | Out-Null
    if ($script:video -eq [IntPtr]::Zero) { return $false }
    $p = [RecWnd]::GetParent($script:video)
    $s = New-Object System.Text.StringBuilder 128
    [RecWnd]::GetClassNameW($p, $s, 128) | Out-Null
    return ($s.ToString() -eq "Chrome_WidgetWin_1")
}

function ModeButtonCentre() {
    RefreshOrigin
    $script:btn = [IntPtr]::Zero
    $cb = [RecWnd+EnumProc]{
        param($c, $lp)
        $s = New-Object System.Text.StringBuilder 128
        [RecWnd]::GetClassNameW($c, $s, 128) | Out-Null
        if ($s.ToString() -eq "Button") { $script:btn = $c }
        return $true
    }
    [RecWnd]::EnumChildWindows($hwnd, $cb, [IntPtr]::Zero) | Out-Null
    if ($script:btn -eq [IntPtr]::Zero) { return $null }
    $r = New-Object RecWnd+RECT
    [RecWnd]::GetWindowRect($script:btn, [ref]$r) | Out-Null
    # Back to client coordinates, which is what ClickAt expects.
    $cx = [int](($r.Left + $r.Right) / 2) - $script:ox
    $cy = [int](($r.Top + $r.Bottom) / 2) - $script:oy
    return @($cx, $cy)
}

if ($Probe -eq 1) {
    Start-Sleep -Milliseconds 3500
    RefreshOrigin
    $b = Grab $script:ox $script:oy $cw $ch
    $b.Save((Join-Path $OutDir "probe.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $b.Dispose()
    Write-Output ("origin={0},{1} mode windowed = {2}; disc diff = {3:P0}" -f `
        $script:ox, $script:oy, (ModeIsWindowed), (DiscDiff))
    Stop-Process -Id $p.Id -Force
    Write-Output "PROBE_DONE"
    exit 0
}

Start-Sleep -Milliseconds 3500     # let the page finish loading
PinWindow

Write-Output "1. composition mode, GIF playing"
CaptureFor 1.4

Write-Output "2. clicking the page buttons (A A B)"
ClickAt $btnA; ClickAt $btnA; ClickAt $btnB
CaptureFor 0.9

Write-Output "3. clicking the empty part of the cut-out -> pause"
EnsurePlaying $false $hole "pause"
CaptureFor 1.5

Write-Output "4. clicking it again -> resume"
EnsurePlaying $true $hole "resume"
CaptureFor 0.9

Write-Output "5. switching to windowed mode"
for ($t = 1; $t -le 3 -and -not (ModeIsWindowed); $t++) {
    $mb = ModeButtonCentre
    if (-not $mb) { Write-Output "  mode button not found"; break }
    PinWindow
    ClickAt $mb
    for ($i = 0; $i -lt 40 -and -not (ModeIsWindowed); $i++) { Start-Sleep -Milliseconds 250 }
    Write-Output ("  try {0}: windowed = {1}" -f $t, (ModeIsWindowed))
    $script:tries++
}
PinWindow
Start-Sleep -Milliseconds 2500      # the page reloads; let it paint
CaptureFor 1.2

Write-Output "6. windowed mode: the same page buttons count (page state reset)"
EnsurePlaying $true $hole "still playing"
ClickAt $btnA
ClickAt $btnB
CaptureFor 0.9

Write-Output "7. windowed mode: the cut-out still toggles the GIF"
EnsurePlaying $false $hole "pause"
CaptureFor 0.8
EnsurePlaying $true $hole "resume"
CaptureFor 0.9

Stop-Process -Id $p.Id -Force
Write-Output ("FRAMES={0} retries={1}" -f $script:frame, $script:tries)
