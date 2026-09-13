# WebView2 + DirectComposition · 三层窗口演示

> 中文版。英文原文见 [README.md](README.md)。

一个 CMake + MSVC 的小工程，演示把 WebView2 页面合成在一个**并非浏览器子窗口**
的原生窗口之上，并在页面里挖一个真正的圆形空洞，让下面那一层透出来。

它可以在 WebView2 的两种托管模式之间运行时切换——这两种模式的行为差别很大，
并排看才看得出区别。

| 部件 | 作用 |
|-------|--------------|
| **`MainWnd`** | 顶层 1100×800 Win32 窗口。持有上下两层，以及底部一条 44px 的控制条。 |
| **`BrowserWnd`** | `MainWnd` 的子窗口，占据客户区「减去底部条带」的区域。它拥有一个 `IDCompositionVisual` 并托管 WebView2——composition 模式走 `ICoreWebView2CompositionController::put_RootVisualTarget`，windowed 模式走传统的 `ICoreWebView2Controller`。 |
| **`VideoWnd`** | 用 Direct2D（`ID2D1HwndRenderTarget`）绘制动图的子窗口。 |
| **`GifAnimator`** | 把 GIF 每一帧预解码成 D2D 位图，再按定时器播放。解码用 GDI+ 而不是 WIC——原因见下文。 |
| **`html/index.html`** | 载入 WebView2 的页面。CSS 径向遮罩在渐变上挖出一个透明的 648px 圆盘，圆盘上还有一个不可见元素把点击回报给宿主。 |
| **点击路由** | 宿主**不**按几何判断点击归属，由页面判断并 `postMessage` 告诉宿主。 |
| **`tools/verify-click-routing.ps1`** | 自动化端到端验证：打真实点击，量真实结果。 |

## 结构

```
MainWnd  (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)
├── BrowserWnd              客户区减去底部 44px 条带
│   ├── VideoWnd            composition 模式：铺满客户区
│   └── Chrome_WidgetWin_0  windowed 模式：由 controller 创建
│       └── Chrome_WidgetWin_1
│           ├── Chrome_RenderWidgetHostHWND
│           ├── Intermediate D3D Window
│           └── VideoWnd    windowed 模式：重挂到这里，压在最底
├── BUTTON (id 1001)        条带：切换托管模式
└── STATIC (id 1002)        条带：说明当前模式
```

三层，一幅画面：GIF 是一个原生窗口，页面画在它上面，页面上的透明圆盘让 GIF 透出来。

### 为什么必须有底部条带

DirectComposition 的 visual 是**合成在本窗口所有子 HWND 之上**的。所以任何 parent
到 `MainWnd`、位置落在 `BrowserWnd` 矩形内的控件，在 composition 模式下都会被页面
盖住——既不显示，也点不到。条带是唯一一条能让普通 Win32 控件在两种模式下都保持
可见、可点的区域。它是结构上的必要条件，不是装饰。

### 挖孔

一个**直径 648px 的圆**，居中，用 CSS
`mask-image: radial-gradient(circle, …)` 挖出，最后 1px 做羽化防锯齿。

`clip-path` 做不到：`polygon()` 只能出直线边，而能给内圈做减法所需的 `evenodd`
填充规则又没法跟形状函数组合。

直径只存在一处——`--hole-d`，由 `html/index.html` 设置、`html/style.css` 使用。
C++ 侧**故意**不留副本：宿主已经不按几何分派点击，也就没理由知道孔在哪。

因为 WebView2 的 rasterization scale 被 pin 到 1.0，CSS 像素 == 物理像素，圆盘正好
落在点击目标上。

### 透明链路

* `ICoreWebView2Controller2::put_DefaultBackgroundColor({0,0,0,0})` 让 WebView 表面
  自身透明。
* `<html>` / `<body>` 设 `background: transparent`。
* `body::before` 画渐变环，上面的遮罩挖出圆盘。
* `VideoWnd` 用 `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR` 把 GIF 1:1 画在
  圆盘上，且落在整像素——半像素偏移会重采样，把 1:1 的画面糊掉。

## 两种托管模式

| | Composition | Windowed |
|---|---|---|
| 接口 | `CreateCoreWebView2CompositionController` | `CreateCoreWebView2Controller` |
| 页面渲染到 | 我们自己拥有的 `IDCompositionVisual` | Chromium 创建的不透明子 HWND |
| 逐像素 alpha | 有 | 没有 |
| 鼠标 | 宿主用 `SendMouseInput` 转发 | 页面自己的 HWND 直接接收 |
| GIF 怎么透出来 | 页面 alpha 让出，露出下面那层 | `VideoWnd` 重挂到页面窗口下、压到 z-order 最底 |

切模式会拆掉 controller 再重建，所以**页面会重新加载，页面内状态（点击计数）会归零**。
这是两种模式创建方式本身决定的，不是 bug。

### windowed 模式要靠合成器混合两个窗口

没有逐像素 alpha 可以依赖，GIF 要显示就只能让 `VideoWnd` 成为**真正绘制页面的那个
窗口**的兄弟——所以它被重挂到最里层的 `Chrome_WidgetWin_*` 下，并压到该窗口子链的
最底，让合成器把页面的 alpha 混合上去。

有两处很讲究，都是实测出来的、不是想当然：

* **不能挂在 `Chrome_WidgetWin_0` 下。** 试过：GIF 会冻住（两张截图逐字节相同）。
  被同级窗口完全覆盖后，DWM 判定它不可见、不再重合成。挂在 `Chrome_WidgetWin_1`
  下就正常，因为压住它的 `Intermediate D3D Window` 是分层窗口、带 alpha。
* **那个窗口是后建的，而且建在 `VideoWnd` 之上。** Chromium 在 controller 返回
  **之后**才建它，于是初始化时做的那次布局必然太早。表现就是**只有 resize 过主窗口，
  页面才渲染正常**。定局交给 `NavigationCompleted`：那是第一个能看见完整窗口树的
  回调。实测三点——controller 创建完成时页面窗口只有 1 个子项、呈现窗口不存在；
  `SetupController()` 结束时 2 个子项、呈现窗口仍不存在；`NavigationCompleted` 时
  3 个子项、呈现窗口已存在且已盖住 `VideoWnd`。所以在 `OnNavCompleted()` 里重排
  一次即可，**无需轮询**：呈现窗口不可能在它所呈现的那次导航结束之后才出现。
  （早期版本用一个上限 20 次、250ms 的 `ArmVideoStackWatch()` 定时器兜底，已删除。）

`VideoWnd` 的尺寸取 `BrowserWnd` 自己的客户区，绝不取目标窗口的：初始化中途
`Chrome_WidgetWin_1` 会报出一个 1608×1529 的错误客户区，照着它给尺寸窗口会捅到
框外面去。

### RasterizationScale 的坑

`put_ShouldDetectMonitorScaleChanges(FALSE)` **必须**在 `put_RasterizationScale(1.0f)`
之前调用。否则 WebView2 会从显示器 DPI 重新推导 scale、悄悄丢掉 pin 值——页面会拿到
一个意料之外的 CSS 视口，`style.css` 里所有固定 `px` 都落不到该落的位置。

这段代码**不能**搬进 `UpdateWebViewBounds()`：那会经由
`RasterizationScaleChanged` → `OnDpiChanged()` → `UpdateWebViewBounds()` 无限递归，
饿死消息循环，表现是 GIF 像卡住不动。

## 点击路由

原则：**宿主永远不猜一个点下面是什么。** 只有页面知道一个坐标落在按钮上还是空白处
——按钮是 HTML，没有 HWND 可供宿主瞄准。

* composition 模式下 `BrowserWnd::WM_NCHITTEST` 对整个客户区返回 `HTCLIENT`，所有
  鼠标消息都进它的 `WndProc`，再一律经 `SendMouseInput` 转给 WebView2。**没有挖孔
  分支。**
* windowed 模式下页面自己的 HWND 直接吃鼠标，上面这套不参与。
* 页面在圆盘上放了一个不可见的 `.video-hit`，`z-index: 0`——在渐变（`-1`）之上、
  所有卡片（`1`）之下。压在圆盘上的控件仍然先拿到点击，只有点在圆盘**空白**处才
  轮到 GIF。
* `.video-hit` 调 `window.chrome.webview.postMessage('video-toggle')`，宿主
  `BrowserWnd::OnWebMessage()` 收到后 `PostMessage` 一个 `WM_LBUTTONDOWN` 给
  `VideoWnd`，切换播放。

因此同一份页面代码驱动两种模式：composition 经 `BrowserWnd`，windowed 经页面自己的
HWND。

`VideoWnd::WM_NCHITTEST` 固定返回 `HTTRANSPARENT`。它的客户区现在覆盖整个窗口，认领
`HTCLIENT` 会吞掉本该属于页面的消息。

### 暂停 / 继续

点圆盘的空白处冻结 GIF，再点一次继续。暂停期间 `Advance()` 不累加时钟，所以恢复是
从屏幕上那一帧继续，而不是把暂停期间的时间一次性补播。画面左上角会画一个暂停角标，
避免把冻结误认为卡顿。

## GIF 解码：用 GDI+，不用 WIC

`GifAnimator` 用 GDI+ 是刻意的。WIC 的 GIF 解码器对某些文件的成串帧会返回**乱码
像素**——在 `marketplace.gif` 里第 48–64 帧解出来是黑白雪花。这一点在 WIC 提供的
每条转换路径上都复现过（原样、逐行 `CopyPixels`、`CreateBitmapFromSource`、绕开
`IWICFormatConverter` 自行读索引表）。GDI+ 解同一批帧是正确的，而且它会把局部帧
合成到画布上，`IWICBitmapFrameDecode` 则完全不这么干。

## 构建

前置条件：

* **Visual Studio 2019 (16.11+)** 或 **VS2022**，带「使用 C++ 的桌面开发」工作负载
  （C++17；工程设了 `CMAKE_CXX_STANDARD 17`）。
* CMake 3.20+
* **Microsoft Edge WebView2 Runtime**（Windows 11 已预装）。

WebView2 SDK 从本机 NuGet 缓存读取，所以**构建不需要 `nuget.exe`、也不需要联网**
——跟 `packages.config` 工程不同。预期位置：

```
%USERPROFILE%\.nuget\packages\microsoft.web.webview2\1.0.3179.45\build\native
```

如果不在那里，要么把该包还原进缓存，要么把 CMake 指到一个 SDK 根目录（含
`build\native` 的那个文件夹）：

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DWV2_SDK_DIR=<sdk-root>
```

### 配置与构建

**VS2019 (x64)：**

```bash
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

**VS2022 (x64)：**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

POST_BUILD 会把 `WebView2Loader.dll`、`html/` 和 `marketplace.gif` 拷到可执行文件旁边。

> 改 `html/` 需要重新构建才会生效——拷贝发生在 POST_BUILD 阶段。

### 运行

```bash
build\bin\Release\webview2_dcomp1.exe
```

## 验证它真的在工作

在这里一张静态截图什么都证明不了：冻住的 GIF 看起来仍然是 GIF。
`tools/verify-click-routing.ps1` 会打真实点击并量结果。

```bash
mkdir vrfy && cd vrfy
powershell -File <repo>\tools\verify-click-routing.ps1 -Mode Composition
powershell -File <repo>\tools\verify-click-routing.ps1 -Mode Windowed
```

每种模式查两件事：

1. **GIF 在动，该停的时候停。** 取挖孔内一个圆盘，报告相隔 0.9s 的两张截图之间变化
   像素的占比：播放中几十个百分点，暂停时**恰好 0%**。脚本会点两次挖孔中心并打印
   这个序列。
2. **页面按钮计数正确。** 连点 A×3、B×1、C×2，并把计数区裁出来存成
   `rt_card_final.png` 供读回。计数器是页面文字，进程外只能靠像素观察。

两种模式实测：点击挖孔暂停↔继续、每次点击恰好一条 toggle 消息、计数读回
`A:3 B:1 C:2 Total:6`——**包括刻意压在挖孔上的按钮 A**。

脚本把截图写到当前目录（`*.png` 已被 git 忽略）。

## 文档

| 文件 | 内容 |
|---|---|
| [`docs/webview2-hosting-modes.md`](docs/webview2-hosting-modes.md) | 窗口结构、两种托管模式、挖孔、点击路由、已知限制。 |
| [`docs/click-routing-verification.md`](docs/click-routing-verification.md) | 点击路由是**怎么**验证的、实测数据、以及走过的弯路。 |

## 文件结构

```
.
├── CMakeLists.txt
├── README.md
├── README.zh-CN.md
├── marketplace.gif                  # 在挖孔里绘制的动图
├── docs/
│   ├── webview2-hosting-modes.md
│   └── click-routing-verification.md
├── html/
│   ├── index.html                   # 载入 WebView2 的页面
│   └── style.css                    # 圆盘遮罩、渐变环、卡片
├── tools/
│   └── verify-click-routing.ps1     # 端到端点击/动画检查
└── src/
    ├── main.cpp                     # wWinMain、消息循环
    ├── App.{h,cpp}                  # DPI、COM、路径、file:// URL
    ├── MainWnd.{h,cpp}              # 顶层窗口 + 44px 条带
    ├── BrowserWnd.{h,cpp}           # WebView2 宿主、两种模式、点击路由
    ├── BrowserWndHandlers.h         # WRL 完成/事件处理器的薄封装
    ├── VideoWnd.{h,cpp}             # D2D GIF 窗口、暂停切换与角标
    ├── GifAnimator.{h,cpp}          # GDI+ 解码、帧表、播放时钟
    ├── PointerInfo.{h,cpp}          # 触摸/笔用的 ICoreWebView2PointerInfo
    ├── util.{h,cpp}                 # 路径、DPI、RAII COM 初始化
    └── app.manifest                 # per-monitor v2 DPI 感知
```

## 可调参数

* [`html/index.html`](html/index.html) 里的 `--hole-d`——挖孔直径，CSS 像素。
* [`html/style.css`](html/style.css) 里的渐变配色与布局。
* [`src/MainWnd.h`](src/MainWnd.h) 里的 `kStripHeight`——底部条带高度。
* [`src/VideoWnd.cpp`](src/VideoWnd.cpp) 里的 `kTimerMs`——GIF 帧间隔（16ms，约 60Hz）。

## 已知限制

* windowed 模式依赖 Chromium 的内部窗口结构（`Chrome_WidgetWin_*`、
  `Intermediate D3D Window`）。将来的 WebView2 Runtime 若改名或改结构，症状会是 GIF
  又被页面盖住。已经没有重试兜底了——拖一下窗口即可恢复（`Resize()` 会再排一次），
  但根因还得再查。
* 切模式会重载页面，页面内状态归零。
* 设备丢失（`D2DERR_RECREATE_TARGET`）时 `VideoWnd` 会重建 render target，但
  `GifAnimator` 的位图绑在旧的那个上、没有重载——设备丢失后 GIF 会变空白。修法是让
  `GifAnimator::Load` 幂等；本次未做。
