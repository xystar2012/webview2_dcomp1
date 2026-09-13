# WebView2 两种托管模式与三层合成说明

本文说明这个 demo 的窗口结构、WebView2 的两种托管模式（Composition / Windowed）、
GIF 如何出现在页面的挖孔里，以及为什么两种模式需要完全不同的实现路径。

## 1. 窗口结构

```
MainWnd                     顶层窗口 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
├── BrowserWnd              占据客户区「减去底部 44px 条带」的区域
│   ├── VideoWnd            绘制 GIF 的原生窗口（D2D HwndRenderTarget）
│   └── Chrome_WidgetWin_1  仅窗口模式下存在，由 ICoreWebView2Controller 创建
├── BUTTON  (id 1001)       条带左侧，切换 WebView2 托管模式
└── STATIC  (id 1002)       条带右侧，当前模式的说明文字
```

`BrowserWnd` 拥有一个 DirectComposition root visual，WebView2 的页面渲染进这个
visual。`VideoWnd` 是 `BrowserWnd` 的子窗口，GIF 由它绘制。

### 为什么底部必须留一条 44px 的条带

DirectComposition 的 visual 是**合成在本窗口所有子 HWND 之上**的。任何 parent 到
`MainWnd`、但位置落在 `BrowserWnd` 矩形内的控件，在 composition 模式下都会被页面
盖住——既不显示也点不到。所以模式切换按钮不能放在页面区域里，必须放在
`BrowserWnd` 矩形之外，也就是这条条带里。条带不是装饰，是这个结构的必要条件。

## 2. 两种托管模式的区别

| | Composition | Windowed |
|---|---|---|
| 创建接口 | `ICoreWebView2Environment3::CreateCoreWebView2CompositionController` | `ICoreWebView2Environment::CreateCoreWebView2Controller` |
| 页面渲染到 | 本窗口的 `IDCompositionVisual` | 自己创建的**不透明子 HWND**（`Chrome_WidgetWin_1`） |
| 逐像素 alpha | 支持 | **不支持**，整个 HWND 不透明 |
| `put_DefaultBackgroundColor` | `{0,0,0,0}`（全透明） | `{255,255,255,255}`（不透明） |
| 鼠标 | 需要宿主用 `SendMouseInput` 转发 | 自己的子 HWND 直接接收 |
| 挖孔如何显示 GIF | 页面 alpha 让出，露出下面的原生层 | 页面上方贴一个裁剪到挖孔的原生窗口 |

两者不能同时存在：切换模式会 `Close()` 掉当前 controller 再重建一个，因此页面会
重新从 `m_url` 加载，页面内状态（计数等）会丢失。切换期间 `m_switchInFlight`
为真，重复点击会被忽略，避免把正在创建的 controller 拆掉。

### RasterizationScale 的坑

`put_ShouldDetectMonitorScaleChanges(FALSE)` **必须在** `put_RasterizationScale(1.0f)`
之前调用。否则 WebView2 会从显示器 DPI 重新推导 scale，把 pin 值丢掉——之前页面
拿到的是 719x496 的 CSS 视口，`style.css` 里所有固定 px（包括 `--video-w` /
`--video-h`）都落不到 `IsInVideoRect()` 期望的位置。

这段代码**不能**放进 `UpdateWebViewBounds()`：那会经由
`RasterizationScaleChanged` → `OnDpiChanged()` → `UpdateWebViewBounds()` 无限递归，
饿死消息循环，表现是 GIF 看起来卡住不动。

## 3. 挖孔（cut-out）

页面中央有一个 648×338 的矩形挖孔，尺寸等于 GIF 原始尺寸。C++ 与 CSS 必须同步：

- `BrowserWnd.h`：`kVideoWidthCss = 648.0` / `kVideoHeightCss = 338.0`
- `html/style.css`：`--video-w: 648px` / `--video-h: 338px`
- `html/index.html`：JS `setProperty('--video-w'/'--video-h', ...)`

CSS 用 `clip-path: polygon(evenodd, <外框>, <内框>)`，内框位置是
`calc(50% - var(--video-w) / 2)`，即居中。

因为 rasterization scale 被 pin 到 1.0，CSS 像素 == 物理像素，所以挖孔在客户区里
就是一个居中的 648×338 矩形。`BrowserWnd::VideoRectPx()` 返回的正是同一个矩形
（读数方式仍然走 `get_RasterizationScale()`，而不是硬编码 1.0，这样一旦 pin 被
丢掉，点击区域会跟着实际渲染走，而不是在挖孔周围悄悄多出一圈点不动的边）。

实测（客户区 1078×700）：挖孔 = `(215,181)-(863,519)`。

## 4. GIF 怎么显示出来

两种模式目标相同——挖孔里是 GIF——但路径完全相反。

### Composition 模式

页面（DComp visual）以逐像素 alpha 合成在 `VideoWnd` 之上，页面的透明挖孔露出
下面的 `VideoWnd`。所以 `VideoWnd` 直接铺满 `BrowserWnd` 的整个客户区，GIF 居中
1:1 画在中间，只有挖孔那一块能被看到。

### Windowed 模式

没有 alpha 可用：controller 的子 HWND 是不透明的，页面上的透明挖孔只会露出它自己
的白色底。**这里不存在真正的"穿透"**，所以改用几何代替 alpha——

`BrowserWnd::LayoutVideoWnd()` 把 `VideoWnd` 缩小到正好等于挖孔矩形，再把它
`HWND_TOP` 压在 controller 的 HWND 之上。GIF 由「一个贴在页面上方的原生窗口」
绘制，而不是从页面下面透出来，最终像素与 composition 模式一致。

两种模式的几何都由 `LayoutVideoWnd()` 统一决定，它在三处被调用：

- `Resize()` 末尾——挖孔随客户区移动，且窗口模式下 controller 可能刚把自己
  的子 HWND 调整过 z-order；
- `SetMode()` 里 `m_mode` 改变之后——旧 controller 已经拆掉，立刻摆放可以让
  GIF 在重建期间不闪空；
- `SetupController()` 里 `put_Bounds()` 之后、`put_IsVisible(TRUE)` 之前——
  `VideoRectPx()` 要读 controller 的 scale，而第一帧显示之前窗口就该就位。

> 这里曾经的错误做法是把 `VideoWnd` 压到 z-order 底部来解决"窗口模式下浏览器被
> 盖住"。那确实让浏览器可见了，代价是 GIF 被不透明的页面 HWND 永久盖住。

## 5. 鼠标路由

Composition 模式下 `VideoWnd` 铺满整个客户区，所以不能让它吃掉鼠标：

- `BrowserWnd::WM_NCHITTEST` 对整个客户区返回 `HTCLIENT`，把所有鼠标消息收进
  自己的 `WndProc`，再手动分派（返回 `HTTRANSPARENT` 会让系统把命中测试交给别的
  窗口，消息就永远到不了这里，也就无法有意地转发给任何一方）。
- 落在挖孔内的点：`MapWindowPoints` 转成 `VideoWnd` 的坐标后 `SendMessageW` 过去。
- 其余的点：`ICoreWebView2CompositionController::SendMouseInput`，坐标是
  WebView2 本地坐标，keys 用 `GET_KEYSTATE_WPARAM`。
- `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` 的 lParam 是**屏幕坐标**，得先 `ScreenToClient`。
- 本进程没有启用 mouse-in-pointer，所以鼠标走 legacy `WM_MOUSE*`；上面的
  `WM_POINTER*` 分支只处理触摸和笔。

Windowed 模式下页面有自己的 HWND 直接吃鼠标，上面这套只对 composition 路径生效。
唯一的例外是挖孔：`VideoWnd` 被裁到挖孔尺寸并且压在最上层，此时它的客户区不会比
画面本身更大，所以在 `WM_NCHITTEST` 里返回 `HTCLIENT`——能命中的点击本来就属于
GIF，正好用来切换暂停。判断方式是「客户区尺寸 == 画面尺寸」：铺满整个客户区时
（composition）两者必然不等，于是返回 `HTTRANSPARENT`，把消息让给 `BrowserWnd`。

## 6. 暂停 / 继续

单击挖孔内的 GIF 区域会 `GifAnimator::TogglePaused()`，再点一次继续。暂停期间
`Advance()` 直接返回当前帧号（不累加时钟，所以恢复后从当前帧继续，而不是"补播"
暂停期间的时间）。画面左上角会画一个暂停角标，避免把冻结误认为卡顿。

`VideoWnd::OnPaint()` 用 `DrawBitmap(..., NEAREST_NEIGHBOR)` 且 origin 取 `floor()`，
保证 1:1 拷贝落在整像素上——半像素偏移会重采样，把 1:1 的画面糊掉。

## 7. 模式切换按钮

条带上的 BUTTON（id 1001）切换模式，STATIC（id 1002）显示当前模式与说明。
`BrowserWnd::SetModeChangedCallback` 在模式被记录时（UI 线程，早于新 controller
就绪）回调 `MainWnd::SyncModeButton()` 更新文字。

拖拽 resize 时两种模式的残影/撕裂表现不同，这是保留这个开关的目的：手动拖边框
对比即可。

## 8. 已知限制

- **窗口模式下挖孔不是真正的 alpha 穿透**，而是"贴在页面上方的原生窗口"。视觉结果
  一致，但如果有人把 `VideoWnd` 的窗口区域和页面挖孔位置调不一致，就会看到错位。
- 切模式会重载页面（controller 重建），页面内状态丢失。
- `VideoWnd::OnPaint()` 在 `D2DERR_RECREATE_TARGET` 时重建 render target，但
  `GifAnimator` 里绑定旧 render target 的位图没有重新加载，设备丢失后 GIF 会变
  空白。修法是把 `GifAnimator::Load` 改成幂等重载（本次未做）。
- `html/` 在 POST_BUILD 时由 CMake 拷到 `bin/Release/html`，改 HTML/CSS 需要重新
  构建才会生效。
