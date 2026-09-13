# WebView2 + DirectComposition · Three-layer window demo

A small CMake + MSVC project showing a WebView2 page composited over a native
window that is **not** the browser's own child, with a real circular hole in the
page letting the layer underneath show through.

Runtime-switchable between WebView2's two hosting models, because they behave
very differently and the difference is worth seeing side by side.

| Piece | What it does |
|-------|--------------|
| **`MainWnd`** | Top-level 1100×800 Win32 window. Owns the two layers and a 44px control strip along the bottom. |
| **`BrowserWnd`** | Child of `MainWnd`, covering the client area minus that strip. Owns an `IDCompositionVisual` and hosts the WebView2 — in composition mode via `ICoreWebView2CompositionController::put_RootVisualTarget`, in windowed mode via the classic `ICoreWebView2Controller`. |
| **`VideoWnd`** | Child window that draws an animated GIF through Direct2D (`ID2D1HwndRenderTarget`). |
| **`GifAnimator`** | Pre-decodes every GIF frame to a D2D bitmap and plays them back on a timer. GDI+ decoder, not WIC — see below. |
| **`html/index.html`** | Page loaded into WebView2. A CSS radial mask carves a transparent 648px disc out of the gradient, and an invisible element over that disc reports clicks back to the host. |
| **Pointer routing** | The host does **not** decide click ownership by geometry. The page does, and tells the host through `postMessage`. |
| **`tools/verify-click-routing.ps1`** | Automated end-to-end check: drives real clicks and measures the result. |

## Architecture

```
MainWnd  (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)
├── BrowserWnd              client area minus the bottom 44px strip
│   ├── VideoWnd            composition mode: fills the client area
│   └── Chrome_WidgetWin_0  windowed mode: created by the controller
│       └── Chrome_WidgetWin_1
│           ├── Chrome_RenderWidgetHostHWND
│           ├── Intermediate D3D Window
│           └── VideoWnd    windowed mode: re-parented here, bottom of the z-order
├── BUTTON (id 1001)        strip: switch hosting mode
└── STATIC (id 1002)        strip: describes the current mode
```

Three layers, one picture: the GIF is a native window, the page is drawn over
it, and the page's transparent disc is what lets the GIF show through.

### Why the bottom strip exists

A DirectComposition visual composites **above every child HWND** of its window.
So any control parented to `MainWnd` inside `BrowserWnd`'s rectangle would be
hidden behind the page in composition mode — invisible *and* unclickable. The
strip is the one band where a plain Win32 control stays both visible and
clickable in either mode. It is a structural necessity, not decoration.

### The cut-out

A **648px-diameter circle**, centred, punched out with a CSS
`mask-image: radial-gradient(circle, …)` — the last 1px feathers the edge so it
does not alias.

`clip-path` cannot do this: `polygon()` only produces straight edges, and the
`evenodd` fill rule that would let you subtract an inner shape cannot be
combined with the shape functions.

The diameter lives in exactly one place — `--hole-d`, set from
`html/index.html` and consumed by `html/style.css`. The C++ side deliberately
does **not** hold a copy: the host no longer routes clicks by geometry, so it
has no reason to know where the hole is.

Because the WebView2 rasterization scale is pinned to 1.0, CSS pixels are 1:1
with physical pixels and the disc lands exactly where the hit target does.

### Transparency plumbing

* `ICoreWebView2Controller2::put_DefaultBackgroundColor({0,0,0,0})` makes the
  WebView surface itself transparent.
* `<html>` / `<body>` set `background: transparent`.
* `body::before` paints the gradient ring; the mask above carves the disc.
* `VideoWnd` draws the GIF 1:1 across the disc with
  `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR`, on whole pixels — a
  half-pixel offset would resample the 1:1 copy and soften it.

## The two hosting modes

| | Composition | Windowed |
|---|---|---|
| Interface | `CreateCoreWebView2CompositionController` | `CreateCoreWebView2Controller` |
| Page renders into | an `IDCompositionVisual` we own | an opaque child HWND Chromium creates |
| Per-pixel alpha | yes | no |
| Mouse | host forwards with `SendMouseInput` | page's own HWND takes it directly |
| GIF shows through by | the page's alpha revealing the layer below | `VideoWnd` re-parented under the page's window, bottom of the z-order |

Switching modes tears the controller down and rebuilds it, so **the page
reloads and any page state (the click counters) resets**. That is inherent to
how the two models are created, not a bug.

### Windowed mode needs the compositor to blend two windows

With no per-pixel alpha to lean on, the GIF can only show if `VideoWnd` is a
true sibling of the window that paints the page — so it is re-parented into the
innermost `Chrome_WidgetWin_*` and pushed to the bottom of that window's child
order, letting the compositor blend the page's alpha over it.

Two things make this fiddly, both measured rather than assumed:

* **Not under `Chrome_WidgetWin_0`.** Tried it: the GIF froze (two captures,
  byte-identical). A child fully covered by a sibling DWM treats as opaque stops
  being recomposited. Under `Chrome_WidgetWin_1` it stays live, because the
  window covering it — `Intermediate D3D Window` — is layered and carries alpha.
* **That window is created late, and on top of `VideoWnd`.** Chromium builds it
  *after* the controller returns, so the layout done during setup is always too
  early. The symptom was that the page only rendered correctly after resizing
  the main window. `NavigationCompleted` is the first callback that sees a
  complete tree — measured: the controller callback sees 1 child of the page
  window and no presenting window, `SetupController()` sees 2 and none, and
  `NavigationCompleted` sees 3 with the presenting window already covering
  `VideoWnd`. So `OnNavCompleted()` re-asserts the stack once. Nothing is
  polled: the presenting window cannot appear after the navigation it presents
  has finished. (An earlier version used a bounded 250ms retry timer,
  `ArmVideoStackWatch()`; it is gone.)

`VideoWnd` is sized from `BrowserWnd`'s own client rect, never from the target
window's: queried mid-init, `Chrome_WidgetWin_1` reports a bogus 1608×1529 client
area and the window would poke far outside the frame.

### RasterizationScale

`put_ShouldDetectMonitorScaleChanges(FALSE)` **must** come before
`put_RasterizationScale(1.0f)`. Otherwise WebView2 re-derives the scale from the
monitor DPI and silently discards the pin — the page gets an unexpected CSS
viewport and every fixed `px` in `style.css` lands somewhere other than where it
was meant to.

This must **not** move into `UpdateWebViewBounds()`: that recurses via
`RasterizationScaleChanged` → `OnDpiChanged()` → `UpdateWebViewBounds()` and
starves the message loop, which presents as the GIF being frozen.

## Pointer routing

The rule: **the host never guesses what is under a point.** Only the page knows
whether a coordinate lands on a button or on empty space — the buttons are HTML
and have no HWND a host could aim at.

* In composition mode `BrowserWnd::WM_NCHITTEST` returns `HTCLIENT` for the whole
  client area, so every mouse message arrives in its `WndProc`, and it forwards
  all of them to the WebView2 via `SendMouseInput`. There is no cut-out branch.
* In windowed mode the page's own HWND takes the mouse directly; none of that
  runs.
* The page carries an invisible `.video-hit` element over the disc, at
  `z-index: 0` — above the gradient (`-1`), below every card (`1`). A control
  that overlaps the disc still wins the click; only a click on the *empty* part
  reaches the GIF.
* `.video-hit` calls `window.chrome.webview.postMessage('video-toggle')`;
  `BrowserWnd::OnWebMessage()` reacts by posting `WM_LBUTTONDOWN` to `VideoWnd`,
  which toggles playback.

The same page code therefore drives both modes: composition goes through
`BrowserWnd`, windowed goes through the page's own HWND.

`VideoWnd::WM_NCHITTEST` always returns `HTTRANSPARENT`. Its client area now
spans the whole window, so claiming `HTCLIENT` would swallow messages that belong
to the page.

### Pause / resume

Clicking the empty part of the disc freezes the GIF; the next click resumes it.
While paused `Advance()` does not accumulate the clock, so resuming continues
from the frame that was on screen rather than fast-forwarding through the pause.
A pause badge is drawn in the picture's top-left corner so a frozen frame is not
mistaken for a slow one.

## GIF decoding: GDI+, not WIC

`GifAnimator` uses GDI+ on purpose. WIC's GIF decoder returns **garbage pixels**
for runs of frames in some files — in `marketplace.gif`, frames 48–64 come back
as black-and-white snow. This was reproduced through every conversion path WIC
offers (raw, per-row `CopyPixels`, `CreateBitmapFromSource`, a hand-rolled
indexed read that bypasses `IWICFormatConverter`). GDI+ decodes the same frames
correctly, and it also composites partial frames onto the canvas, which
`IWICBitmapFrameDecode` does not do at all.

## Build

Prerequisites:

* **Visual Studio 2019 (16.11+)** or **VS2022** with the "Desktop development
  with C++" workload (C++17; the project sets `CMAKE_CXX_STANDARD 17`).
* CMake 3.20+
* **Microsoft Edge WebView2 Runtime** (pre-installed on Windows 11).

The WebView2 SDK is read from the local NuGet cache, so **no `nuget.exe` and no
network access are needed to build** — unlike a `packages.config` project. The
expected location is:

```
%USERPROFILE%\.nuget\packages\microsoft.web.webview2\1.0.3179.45\build\native
```

If it is not there, either restore that package into the cache or point CMake at
an SDK root (the folder containing `build\native`):

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DWV2_SDK_DIR=<sdk-root>
```

### Configure & build

**VS2019 (x64):**

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

**VS2022 (x64):**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The post-build step copies `WebView2Loader.dll`, `html/` and `marketplace.gif`
next to the executable.

> Editing `html/` requires a rebuild — the copy happens in the post-build step.

### Run

```bash
build\bin\Release\webview2_dcomp1.exe
```

## Verifying it actually works

A static screenshot proves nothing here: a frozen GIF still looks like a GIF.
`tools/verify-click-routing.ps1` drives real clicks and measures the outcome.

```bash
mkdir vrfy && cd vrfy
powershell -File <repo>\tools\verify-click-routing.ps1 -Mode Composition
powershell -File <repo>\tools\verify-click-routing.ps1 -Mode Windowed
```

It checks two things per mode:

1. **The GIF is animating, and stops when it should.** It samples a disc inside
   the cut-out and reports the fraction of pixels that change between two
   captures 0.9s apart: tens of percent while playing, exactly **0%** while
   paused. The script clicks the cut-out centre twice and prints the sequence.
2. **The page buttons count clicks.** It clicks A×3, B×1, C×2 and saves a crop of
   the counters to `rt_card_final.png` for reading back. The counters are page
   text, so pixels are the only way to observe them from outside the process.

Measured in both modes: cut-out clicks toggle pause/resume, one toggle message
per click, and the counters read `A:3 B:1 C:2 Total:6` — including button A,
which deliberately overlaps the cut-out.

The scripts write their captures into the current directory (all `*.png` is
git-ignored).

## Docs

| File | Contents |
|---|---|
| [`docs/webview2-hosting-modes.md`](docs/webview2-hosting-modes.md) | Window structure, both hosting models, the cut-out, pointer routing, known limits. |
| [`docs/click-routing-verification.md`](docs/click-routing-verification.md) | How the click routing was verified, the measurements, and the dead ends. |

## File map

```
.
├── CMakeLists.txt
├── README.md
├── README.zh-CN.md
├── marketplace.gif                  # the animated GIF drawn in the cut-out
├── docs/
│   ├── webview2-hosting-modes.md
│   └── click-routing-verification.md
├── html/
│   ├── index.html                   # page loaded into WebView2
│   └── style.css                    # disc mask, gradient ring, cards
├── tools/
│   └── verify-click-routing.ps1     # end-to-end click/animation check
└── src/
    ├── main.cpp                     # wWinMain, message loop
    ├── App.{h,cpp}                  # DPI, COM, paths, the file:// URL
    ├── MainWnd.{h,cpp}              # top-level window + the 44px strip
    ├── BrowserWnd.{h,cpp}           # WebView2 host, both modes, pointer routing
    ├── BrowserWndHandlers.h         # WRL completion/event handler shims
    ├── VideoWnd.{h,cpp}             # D2D GIF window, pause toggle, badge
    ├── GifAnimator.{h,cpp}          # GDI+ decode, frame list, playback clock
    ├── PointerInfo.{h,cpp}          # ICoreWebView2PointerInfo for touch/pen
    ├── util.{h,cpp}                 # paths, DPI, RAII COM init
    └── app.manifest                 # per-monitor v2 DPI awareness
```

## Knobs

* `--hole-d` in [`html/index.html`](html/index.html) — diameter of the cut-out,
  in CSS pixels.
* Gradient palette and layout in [`html/style.css`](html/style.css).
* `kStripHeight` in [`src/MainWnd.h`](src/MainWnd.h) — bottom strip height.
* `kTimerMs` in [`src/VideoWnd.cpp`](src/VideoWnd.cpp) — GIF frame cadence
  (16ms, ~60Hz).

## Known limits

* Windowed mode depends on Chromium's internal window structure
  (`Chrome_WidgetWin_*`, `Intermediate D3D Window`). A future WebView2 runtime
  could rename or restructure them; the symptom would be the GIF being covered by
  the page again. There is no retry fallback any more — dragging the window
  recovers it (`Resize()` re-runs the layout), but the cause would still need
  chasing.
* Switching modes reloads the page and resets page state.
* On `D2DERR_RECREATE_TARGET` `VideoWnd` rebuilds its render target, but
  `GifAnimator`'s bitmaps are bound to the old one and are not reloaded — after a
  device loss the GIF goes blank. The fix is to make `GifAnimator::Load`
  idempotent; not done.
