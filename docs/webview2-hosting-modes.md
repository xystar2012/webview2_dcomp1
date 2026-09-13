# WebView2 两种托管模式与三层合成说明

本文说明这个 demo 的窗口结构、WebView2 的两种托管模式（Composition / Windowed）、
GIF 如何出现在页面的挖孔里，以及为什么两种模式需要完全不同的实现路径。

## 1. 窗口结构

```
MainWnd                     顶层窗口 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN
├── BrowserWnd              占据客户区「减去底部 44px 条带」的区域
│   ├── VideoWnd            composition 模式下在这里，铺满客户区
│   └── Chrome_WidgetWin_0  windowed 模式下由 controller 创建
│       └── Chrome_WidgetWin_1
│           ├── Chrome_RenderWidgetHostHWND
│           ├── Intermediate D3D Window
│           └── VideoWnd     windowed 模式下重挂到这里，压在最底
├── BUTTON  (id 1001)       条带左侧，切换 WebView2 托管模式
└── STATIC  (id 1002)       条带右侧，当前模式的说明文字
```

`BrowserWnd` 拥有一个 DirectComposition root visual，composition 模式下 WebView2
的页面渲染进这个 visual，`VideoWnd` 是它的子窗口、铺满客户区。

windowed 模式下页面画在不透明的子 HWND 里，`VideoWnd` 会被**重挂**到最里面那个
`Chrome_WidgetWin_1` 下、压到最底，让合成器用页面的 alpha 去混合——见第 4 节。

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
| `put_DefaultBackgroundColor` | `{0,0,0,0}`（全透明） | 目前也设 `{0,0,0,0}`（实验：让唯一变量变成"不透明子 HWND 能否传递 alpha"） |
| 鼠标 | 需要宿主用 `SendMouseInput` 转发 | 自己的子 HWND 直接接收 |
| 挖孔如何显示 GIF | 页面 alpha 让出，露出下面的原生层 | `VideoWnd` 重挂到页面窗口下、压到最底，由合成器按页面 alpha 混合 |

两者不能同时存在：切换模式会 `Close()` 掉当前 controller 再重建一个，因此页面会
重新从 `m_url` 加载，页面内状态（计数等）会丢失。切换期间 `m_switchInFlight`
为真，重复点击会被忽略，避免把正在创建的 controller 拆掉。

### RasterizationScale 的坑

`put_ShouldDetectMonitorScaleChanges(FALSE)` **必须在** `put_RasterizationScale(1.0f)`
之前调用。否则 WebView2 会从显示器 DPI 重新推导 scale，把 pin 值丢掉——之前页面
拿到的是 719x496 的 CSS 视口，`style.css` 里所有固定 px（包括 `--hole-d`）都跟
物理像素对不上，挖孔和 `.video-hit` 也就对不齐了。

这段代码**不能**放进 `UpdateWebViewBounds()`：那会经由
`RasterizationScaleChanged` → `OnDpiChanged()` → `UpdateWebViewBounds()` 无限递归，
饿死消息循环，表现是 GIF 看起来卡住不动。

## 3. 挖孔（cut-out）

页面中央有一个**直径 648px 的圆形**挖孔，直径等于 GIF 的宽度。GIF 按 1:1 画在圆
里，横向正好铺满，上下露出 `VideoWnd` 自己的底色。做成圆的而不是方的，是为了让
合成器真正去处理一条非矩形的 alpha 边，而不是一个轴对齐的方框。

尺寸只由页面一处决定：

- `html/index.html`：JS `setProperty('--hole-d', '648px')`
- `html/style.css`：`--hole-d` 的默认值，以及用它的 `mask-image`

C++ 侧**不再**持有挖孔几何。宿主不按几何分派点击（见第 5 节），所以它不需要知道
挖孔在哪——只有页面需要，而页面已经知道了。

CSS 用 `mask-image: radial-gradient(circle, ...)`：半径内 alpha 0、半径外不透明，
最后 1px 做羽化防锯齿。**不能**用 `clip-path`——`polygon()` 只能出直线边，而
`evenodd` 填充规则又没法跟形状函数（`circle()` 等）组合。

因为 rasterization scale 被 pin 到 1.0，CSS 像素 == 物理像素，所以这个圆在客户区
里就是正圆，`.video-hit` 也能和它精确对齐。

实测（客户区 1078×700，圆盘占满大半）：圆心 `(539,350)`、半径 324。

## 4. GIF 怎么显示出来

两种模式目标相同——挖孔里是 GIF——但路径完全相反。

### Composition 模式

页面（DComp visual）以逐像素 alpha 合成在 `VideoWnd` 之上，页面的透明挖孔露出
下面的 `VideoWnd`。所以 `VideoWnd` 直接铺满 `BrowserWnd` 的整个客户区，GIF 居中
1:1 画在中间，只有挖孔那一块能被看到。

### Windowed 模式

没有 alpha 可用：controller 的子 HWND 是不透明的，页面上的透明挖孔只会露出它自己
的底。**这里不存在真正的"穿透"**，改成把 `VideoWnd` 移动到**真正绘制页面的那个
窗口**下面，做它的兄弟，让合成器拿页面的 alpha 去混合两层。

Chromium 的窗口是嵌套的，`VideoWnd` 要挂到最里面那个：

```
webview2_dcomp1.BrowserWnd
  Chrome_WidgetWin_0                 <- 包裹层，本窗口的直接子窗口
    Chrome_WidgetWin_1               <- 页面绘制在这里
      Chrome_RenderWidgetHostHWND
      Intermediate D3D Window        <- 真正呈现页面的那个窗口（后建）
```

`LayoutVideoWnd()` 把 `VideoWnd` `SetParent` 到 `Chrome_WidgetWin_1`，然后
`HWND_BOTTOM` 压到该窗口子链的最底。两种模式都铺满整个客户区，区别只在父窗口和
叠放次序——因为子 HWND 只跟自己的兄弟比 z-order。

- `Resize()` 末尾——挖孔随客户区移动，且窗口模式下 controller 可能刚把自己
  的子 HWND 调整过；
- `SetMode()` 里 `m_mode` 改变之后——旧 controller 已经拆掉，立刻摆放可以让
  GIF 在重建期间不闪空；
- `SetupController()` 里 `put_Bounds()` 之后、`put_IsVisible(TRUE)` 之前；
- `ArmVideoStackWatch()` 的 250ms 定时器——见下。

> **不能挂在 `Chrome_WidgetWin_0` 下。** 试过：挂在那里并压到最底，GIF 会**冻住**
> （前后两张截图逐字节相同）。被上面那层不透明的兄弟窗口完全覆盖后，DWM 判定它
> 不可见、不再重合成。挂在 `Chrome_WidgetWin_1` 下就好了，因为压住它的是分层的
> `Intermediate D3D Window`，带 alpha，混合照常。
>
> **`Intermediate D3D Window` 是后建的。** `SetupController()` 里那次布局一定早于
> 它，于是它一出现就把 `VideoWnd` 盖住——表现是**只有 resize 过主窗口才正常**
> （resize 会再跑一次布局）。`ArmVideoStackWatch()` 用定时器反复重排直到
> `VideoStackSettled()` 为真（该窗口已存在**且** `VideoWnd` 是子链最后一项），
> settle 后即 `KillTimer`，不是常驻轮询。

尺寸取**自己的** `GetClientRect()`：`Chrome_WidgetWin_1` 初始化中途会报出
1608×1529 的临时客户区，照它给尺寸会让 `VideoWnd` 捅到窗口外。

## 5. 鼠标路由

**宿主不按几何猜点击归属。** 只有页面知道一个点落在按钮上还是落在空白处——按钮
是 HTML，没有 HWND 可以瞄准，宿主的几何判断看不见它。

Composition 模式下 `VideoWnd` 铺满整个客户区，所以不能让它吃掉鼠标：

- `BrowserWnd::WM_NCHITTEST` 对整个客户区返回 `HTCLIENT`，把所有鼠标消息收进
  自己的 `WndProc`，再一律转给 WebView2（返回 `HTTRANSPARENT` 会让系统把命中测试
  交给别的窗口，消息就永远到不了这里）。**没有按挖孔分派的分支。**
- 鼠标走 legacy `WM_MOUSE*`（本进程没启用 mouse-in-pointer），用
  `ICoreWebView2CompositionController::SendMouseInput` 转发，坐标是 WebView2 本地
  坐标，keys 用 `GET_KEYSTATE_WPARAM`。
- `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` 的 lParam 是**屏幕坐标**，得先 `ScreenToClient`。
- `WM_POINTER*` 分支只处理触摸和笔。

Windowed 模式下页面自己的 HWND 直接吃鼠标，上面这套不参与。

### 那"点挖孔暂停 GIF"怎么走

由页面决定，而不是靠宿主量几何：

- 页面有一个不可见的 `.video-hit` 盖住挖孔，`z-index: 0`——在渐变背景（`-1`）
  之上、所有卡片（`1`）之下。压在圆上的控件仍然先拿到点击，只有圆内**空白**部分
  落到它上面。
- 它 `postMessage('video-toggle')`，宿主 `BrowserWnd::OnWebMessage()` 收到后
  `PostMessageW(m_videoWnd, WM_LBUTTONDOWN)`，`GifAnimator::TogglePaused()`。

`VideoWnd::WM_NCHITTEST` 固定返回 `HTTRANSPARENT`：它的客户区现在就是整个客户区，
认领 `HTCLIENT` 会吞掉本该属于页面的消息。

这样两种模式共用同一份页面代码：composition 由 `BrowserWnd` 收下再转发，
windowed 由页面自己的 HWND 直接收。

详细验证见 `docs/click-routing-verification.md`。

## 6. 暂停 / 继续

单击挖孔内的**空白**处（即落在 `.video-hit` 上的点击，见第 5 节）会
`GifAnimator::TogglePaused()`，再点一次继续。暂停期间 `Advance()` 直接返回当前
帧号（不累加时钟，所以恢复后从当前帧继续，而不是"补播"暂停期间的时间）。画面
左上角会画一个暂停角标，避免把冻结误认为卡顿。

`VideoWnd::OnPaint()` 用 `DrawBitmap(..., NEAREST_NEIGHBOR)` 且 origin 取 `floor()`，
保证 1:1 拷贝落在整像素上——半像素偏移会重采样，把 1:1 的画面糊掉。

## 7. 模式切换按钮

条带上的 BUTTON（id 1001）切换模式，STATIC（id 1002）显示当前模式与说明。
`BrowserWnd::SetModeChangedCallback` 在模式被记录时（UI 线程，早于新 controller
就绪）回调 `MainWnd::SyncModeButton()` 更新文字。

拖拽 resize 时两种模式的残影/撕裂表现不同，这是保留这个开关的目的：手动拖边框
对比即可。

## 8. 已知限制

- **窗口模式下依赖 Chromium 的内部窗口结构**：`VideoWnd` 会被重挂到
  `Chrome_WidgetWin_1` 下并压到最底。这是实测出来的结构，Chromium 版本升级后窗口
  命名或嵌套方式若变化，`FindPageWnd()` / `VideoStackSettled()` 可能失效；届时的
  表现是 GIF 又被页面盖住。`ArmVideoStackWatch()` 有次数上限，不会死循环。
- 切模式会重载页面（controller 重建），页面内状态（计数等）丢失。
- `VideoWnd::OnPaint()` 在 `D2DERR_RECREATE_TARGET` 时重建 render target，但
  `GifAnimator` 里绑定旧 render target 的位图没有重新加载，设备丢失后 GIF 会变
  空白。修法是把 `GifAnimator::Load` 改成幂等重载（本次未做）。
- `html/` 在 POST_BUILD 时由 CMake 拷到 `bin/Release/html`，改 HTML/CSS 需要重新
  构建才会生效。
