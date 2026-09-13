# 点击路由与 windowed 叠层修复——验证记录

本文记录这一阶段修的两个问题、修法，以及**怎么验证的**。验证脚本在
`tools/verify-click-routing.ps1`，可以照第 6 节复现。

配套阅读：`docs/webview2-hosting-modes.md`（窗口结构与两种托管模式）。

---

## 1. 这一阶段修了什么

| # | 现象 | 状态 |
|---|---|---|
| 1 | composition 模式下，与中央挖孔重叠的页面按钮点不动 | 已修，已验证 |
| 2 | windowed 模式下，点击挖孔不能暂停 GIF | 已修，已验证 |
| 3 | （附带）windowed 模式下只有 resize 过主窗口，浏览器才正常渲染 | 已修，已验证 |

问题 1 和 2 是同一个根因的两面。

---

## 2. 根因：宿主不该按几何猜点击归属

原来的做法是宿主按几何分派：

- `BrowserWnd::WM_NCHITTEST` 在挖孔矩形内返回 `HTTRANSPARENT`，挖孔内一律
  `SendMessage` 给 `VideoWnd`；
- 其余的点才 `SendMouseInput` 给 WebView2。

这在两处同时错：

- **挖孔内的页面控件收不到点击**。每个落在圆内的点都被判给了 GIF，哪怕它其实
  压在一个按钮上——按钮是 HTML，没有 HWND 可以瞄准，宿主的几何判断根本看不见它。
- **GIF 的点击判据不可靠**。判据是「点是否落在挖孔矩形内」，而 windowed 模式下
  页面 HWND 在 `VideoWnd` 之上，`VideoWnd` 自己永远收不到鼠标消息，所以「点挖孔
  暂停」在 windowed 模式下根本不成立。

根子在于**几何判断只描述了「挖孔在哪」，没描述「这个点上有什么」**。只有页面自
己知道一个点落在按钮上还是落在空白处。

**修法**：把归属判断从宿主挪到页面。

- 页面放一个不可见的 `.video-hit` 元素盖住挖孔（`style.css`），`z-index: 0`——
  在渐变背景（`-1`）之上、在所有卡片（`1`）之下。所以压在圆上的控件仍然先拿到
  点击，只有圆内的**空白**部分才落到 `.video-hit`。
- `.video-hit` 的 click 处理器 `postMessage('video-toggle')`。
- `BrowserWnd::OnWebMessage()` 收到后 `PostMessageW(m_videoWnd, WM_LBUTTONDOWN)`
  给 `VideoWnd`，`GifAnimator::TogglePaused()`。

宿主端因此删掉了 `HoleCirclePx()` / `IsInHole()` / `kHoleDiameterCss`，鼠标处理器
不再分支，一律转给 WebView2。`VideoWnd::WM_NCHITTEST` 固定返回 `HTTRANSPARENT`：
它的客户区现在就是整个客户区，让它认领 `HTCLIENT` 会吞掉本该属于页面的消息。

这个设计在两种模式下都成立：composition 模式点击由 `BrowserWnd` 收下再转发给
WebView2；windowed 模式页面自己的 HWND 直接收点击。两条路都走同一份页面代码。

---

## 3. 附带问题：windowed 叠层与「resize 后才渲染」

windowed 模式要看见 GIF，`VideoWnd` 必须和**真正绘制页面的那个窗口**做兄弟，并
压在它下面，这样合成器才能拿页面的 alpha 去混合。Chromium 的窗口是嵌套的：

```
webview2_dcomp1.BrowserWnd
  Chrome_WidgetWin_0                 <- 包裹层，本窗口的直接子窗口
    Chrome_WidgetWin_1               <- 页面绘制在这里
      Chrome_RenderWidgetHostHWND
      Intermediate D3D Window        <- 真正呈现页面的那个窗口（后建）
```

两个坑：

1. **不能挂在 `Chrome_WidgetWin_0` 下。** 试过把 `VideoWnd` 挂在它下面、压到最底，
   GIF 会**冻住**（前后两张截图逐字节相同）。原因是被上面那层不透明的兄弟窗口完
   全覆盖后，DWM 判定它不可见、不再重合成——它没被盖"花"，是停了。挂在
   `Chrome_WidgetWin_1` 下就没这个问题，因为压住它的 `Intermediate D3D Window`
   是分层窗口、带 alpha，混合照常进行。

2. **`Intermediate D3D Window` 是后建的。** `SetupController()` 里那次
   `LayoutVideoWnd()` 一定早于它，于是它一出现就把 `VideoWnd` 盖住了——表现就是
   **只有 resize 过主窗口才正常**（resize 会再跑一次布局）。

尺寸取**自己的** `GetClientRect()`，不取目标窗口的：`Chrome_WidgetWin_1` 在初始化
中途会报出 1608×1529 的临时客户区，照着它给 `VideoWnd` 就会捅到窗口外面去；
Chromium 之后才收敛到真正的 1078×700。

**定局的那次布局放在 `OnNavCompleted()`。** 原先用一个上限 20 次、250ms 的
`ArmVideoStackWatch()` 定时器反复重排直到 `VideoStackSettled()` 为真；后来测出
`NavigationCompleted` 就是第一个能看见完整窗口树的回调，定时器随即删除。在三个
回调点各打一次探针，看到的东西是：

| 回调 | 页面窗口子项 | 呈现窗口存在 | `VideoWnd` 在最底 |
|---|---|---|---|
| controller 创建完成 | 1 | 否 | 否 |
| `SetupController()` 结束 | 2 | 否 | 是 |
| `NavigationCompleted` | 3 | **是** | **否** ← 被呈现窗口压住 |

于是在 `OnNavCompleted()` 里重排一次即可，且不需要轮询——呈现窗口不可能在它所呈现
的那次导航结束之后才出现。日志里的证据链：`settled=0`（`SetupController`）→
`settled=1`（`OnNavCompleted`），且全程没有 resize、之后也没有任何定时器动作。

---

## 4. 验证方法

`tools/verify-click-routing.ps1`，指定模式跑一遍，做四件事。

### 4.1 动画量 anim%

取挖孔内半径 260px 的圆盘，以 8px 步长采样，比较相隔 0.9s 的两张截图，逐像素差
`|ΔR|+|ΔG|+|ΔB| > 24` 记为变化，输出变化占比：

- **播放中：几十个百分点**（本 GIF 帧间差异大，实测 41%–97%，随截图时相位波动）
- **暂停中：0%**

这是判「到底有没有在动」的硬指标。**单张截图不能证明在动**——这一阶段吃过一次
亏：一张截图看着像 GIF 渲染出来了，实际是冻的静止帧（两张截图 MD5 完全相同）。
所以任何"看得见"的结论都必须配一次 anim% 测量。

### 4.2 点击注入与归属确认

`SetCursorPos` + `mouse_event(LEFTDOWN/LEFTUP)`。每次点击前用 `WindowFromPoint`
+ `GetParent` 上溯确认该点确实落在本进程窗口内，否则打印 `CLICK-SKIPPED` 而不是
把「点丢了」误读成「功能坏了」。

### 4.3 按钮坐标由 CSS 盒模型算出

按钮是 HTML，没有 HWND 可瞄准。**先用色彩扫描找**（找 `#6366f1 -> #06b6d4` 的
特征色再聚类），**不可靠，已弃用**：A、B 两个按钮之间那条 8px 缝隙正好压在挖孔
边缘上，截图时 GIF 当前帧的像素会把缝"桥"起来，两个按钮被聚成一个簇，质心落在
缝隙里——也就是**按钮之间的空白上**，点下去什么都不是。改成按 CSS 直接算：

```
.card-actions { top:24px; right:24px; width:400px }
.card         { padding:18px 20px }
.btn-row      { gap:8px; margin-top:8px }
.btn          { flex:1 1 0 }
```

三个等宽按钮铺满 `400-40=360px` 内容宽、两条 8px 缝，行中心 y≈146。算出来
客户区 `(731,146) (854,146) (977,146)`，屏幕 `(782,231) (905,231) (1028,231)`，
与色彩扫描在顺利时给出的结果一致。

### 4.4 计数读回

计数器是页面文字，进程外只能读像素：脚本把右上卡片从最终截图里裁出来存成
`rt_card_final.png`，再**读图确认**数值。这是"按钮真的计数了"这一结论的直接证据。

### 4.5 消息计数

`BrowserWnd::OnWebMessage` 的日志行带自增序号
（`page reports a click on the GIF area (#n)`）。一次点击该产生**一条**消息；多于
一条说明页面重复触发或宿主重复处理。

---

## 5. 结果

点击序列固定为：挖孔中心 ×1（暂停）→ ×1（继续）→ 按钮 **A×3、B×1、C×2** →
挖孔中心 ×1（暂停）。

| 检查项 | Composition | Windowed |
|---|---|---|
| `1_playing` anim% | 41–97% | 49–86% |
| 点挖孔后 anim% | **0%** | **0%** |
| 再点一次 anim% | 65–97% | 57–93% |
| 第三次点 anim% | **0%** | **0%** |
| 计数读回 | `A:3 B:1 C:2 Total:6` | `A:3 B:1 C:2 Total:6` |
| toggle 消息数 / 挖孔点击数 | 3 / 3 | 3 / 3 |

两种模式结果一致：

- 点击挖孔**暂停 ↔ 继续**，暂停时 anim% 严格为 0，恢复后回到几十个百分点；
- 三个按钮各自计数、互不串号，总数 6，**包括压在挖孔上的按钮 A**——这正是问题 1
  的验证点；
- 每次点挖孔**恰好一条** toggle 消息，没有重复触发。

按钮的 y 坐标是算出来的（≈146），不是量出来的，所以它有十几像素的余量；按钮高
约 40px，容得下。

---

## 6. 复现

```bash
cmake --build build --config Release

# 从任意临时目录跑，截图落在当前目录
cd /tmp && mkdir -p vrfy && cd vrfy
powershell -File <repo>/tools/verify-click-routing.ps1 -Mode Composition
powershell -File <repo>/tools/verify-click-routing.ps1 -Mode Windowed
```

看两处：stdout 里 `1_playing` 的 anim% 应远大于 0、点两次后为 0；再打开
`rt_card_final.png` 确认计数。

`html/` 由 CMake 在 POST_BUILD 拷到 `bin/Release/html`，**改 HTML/CSS 必须重新
构建**才会生效。

---

## 7. 过程中踩的坑

留档，避免下次重复：

- **单张截图不能证明在动。** 必须配 anim%（§4.1）。
- **色彩聚类找按钮会被 GIF 帧干扰。** 按钮缝隙压在挖孔边缘上，GIF 像素会桥接聚类
  （§4.3）。改用 CSS 盒模型算坐标。
- **坐标帧要分清。** 截图是**整窗**的，客户端坐标 → 位图要加边框偏移 `(dx,dy)`，
  客户端坐标 → 屏幕要加客户区原点 `(ox,oy)`。混用会得到一个固定的几十像素偏移，
  表现为「按钮位置整体偏一点」。
- **PowerShell 里 `Write-Output` 会污染返回值。** 被 `@(Func ...)` 捕获时，函数内
  的 `Write-Output` 会混进返回数组；用 `GetPixel` 下标去取就会拿到空值。
- **`Measure` 是 `Measure-Object` 的内置别名**，优先级高于同名函数；自己的函数别
  叫这个名字（脚本里叫 `ReadAnim`）。
- **启动后的第一次点击会被窗口激活吞掉。**
- **调试日志是追加写的**，不清理就跨轮累加。曾出现「10 次点击记到 11 条 toggle
  消息」，清空日志后复测是严格的 3 比 3——不是重复触发，是上一轮的残留。

---

## 8. 本次未验证 / 遗留

- **窗口极小或 DPI 变化时**的点击路由没测；按钮坐标是硬编码的 CSS 推导，窗口太小
  时卡片本身会被挤掉。
- **`GifAnimator` 的设备丢失重载**仍未做：`VideoWnd::OnPaint()` 在
  `D2DERR_RECREATE_TARGET` 时重建 render target，但绑定旧 RT 的位图没有重载，设备
  丢失后 GIF 会变空白。
- 切模式会重建 controller，页面重载，**页面内的计数会归零**（这是既有行为）。
