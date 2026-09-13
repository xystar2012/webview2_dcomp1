#include "BrowserWnd.h"
#include "util.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <shlwapi.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <cmath>
#include <sstream>
#include <fstream>

namespace
{
    void dlog(const wchar_t* msg)
    {
        // Tiny log appended to %TEMP%\webview2_dcomp1_debug.log. Useful for
        // diagnosing silent WebView2 init failures.
        wchar_t path[MAX_PATH];
        DWORD n = GetTempPathW(MAX_PATH, path);
        if (n == 0 || n >= MAX_PATH) return;
        std::wstring p(path, n);
        p += L"webview2_dcomp1_debug.log";
        std::ofstream f(p, std::ios::app | std::ios::binary);
        // UTF-16LE BOM + payload + newline. Keeps the log readable from
        // notepad without any extra dependencies.
        const wchar_t bom = 0xFEFF;
        f.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
        std::wstring line = std::wstring(msg) + L"\n";
        f.write(reinterpret_cast<const char*>(line.c_str()),
                std::streamsize(line.size() * sizeof(wchar_t)));
        OutputDebugStringW(msg);
    }

    // First Chrome_WidgetWin_* child of w, ignoring skip. WebView2 nests these
    // wrapper windows, so "the window that paints the page" is found by
    // following them down as far as they go.
    HWND FirstChromeChild(HWND w, HWND skip)
    {
        for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        {
            if (c == skip) continue;
            wchar_t cls[128]{};
            if (GetClassNameW(c, cls, 128) == 0) continue;
            if (std::wstring(cls).rfind(L"Chrome_WidgetWin_", 0) == 0) return c;
        }
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
//  BrowserWnd
// ---------------------------------------------------------------------------

BrowserWnd::~BrowserWnd() = default;

bool BrowserWnd::Create(HWND parent, const RECT& bounds, const std::wstring& url)
{
    m_url = url;

    wchar_t dbg[200];
    std::swprintf(dbg, 200, L"[BrowserWnd] Create: input bounds=(%ld,%ld)-(%ld,%ld)",
                  bounds.left, bounds.top, bounds.right, bounds.bottom);
    OutputDebugStringW(dbg);

    WNDCLASSEXW wc{};
    // No CS_OWNDC: DComp renders directly into the window surface, and
    // CS_OWNDC would lock in a GDI DC that fights with composition.
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &BrowserWnd::StaticWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"webview2_dcomp1.BrowserWnd";
    static ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    // The window is created small and resized to the parent client area
    // by MainWnd. DComp + WebView2 init are deferred to the first
    // WM_SIZE so we know the HWND has a real on-screen size.
    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Browser",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
    parent, nullptr, wc.hInstance, this);
    if (!m_hwnd) return false;

    RECT wr{}; GetWindowRect(m_hwnd, &wr);
    std::swprintf(dbg, 200, L"[BrowserWnd] Create: created at (%ld,%ld)-(%ld,%ld) dpi=%u",
                  wr.left, wr.top, wr.right, wr.bottom, GetDpiForWindow(m_hwnd));
    OutputDebugStringW(dbg);

    return true;
}

void BrowserWnd::InitOnceSized()
{
    if (m_initialized || !m_hwnd) return;
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    wchar_t buf[160];
    std::swprintf(buf, 160, L"[BrowserWnd] InitOnceSized client=%ldx%ld", rc.right, rc.bottom);
    dlog(buf);
    m_initialized = true;
    dlog(L"[BrowserWnd] InitOnceSized begin");
    if (FAILED(InitDComp())) { dlog(L"[BrowserWnd] InitDComp FAILED"); return; }
    if (FAILED(InitWebView2())) { dlog(L"[BrowserWnd] InitWebView2 FAILED"); return; }
}

void BrowserWnd::UpdateVisualBounds()
{
    if (!m_hwnd || !m_rootVisual) return;
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    D2D_RECT_F clip{ 0.0f, 0.0f,
        std::max<LONG>(1, rc.right - rc.left) * 1.0f,
        std::max<LONG>(1, rc.bottom - rc.top) * 1.0f };
    m_rootVisual->SetOffsetX(0.0f);
    m_rootVisual->SetOffsetY(0.0f);
    m_rootVisual->SetClip(clip);
}

void BrowserWnd::UpdateWebViewBounds()
{
    if (!m_hwnd || !m_controller3) return;
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    RECT bounds = {
        0, 0,
        std::max<LONG>(1, rc.right - rc.left),
        std::max<LONG>(1, rc.bottom - rc.top) };
    // RAW_PIXELS is the simplest mode: bounds are in physical pixels and
    // the WebView2 renders at physical resolution. USE_RASTERIZATION_SCALE
    // requires the host to feed DIPs which is fragile across DPI changes.
    //
    // RasterizationScale is pinned to 1.0 once, in OnControllerCreated(), so
    // that CSS pixels == physical pixels. Do NOT set it here: doing so fires
    // RasterizationScaleChanged -> OnDpiChanged() -> UpdateWebViewBounds(),
    // which re-enters this function forever and starves the message loop
    // (VideoWnd's WM_TIMER never runs, so the GIF looks frozen).
    m_controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
    m_controller3->put_Bounds(bounds);
    RECT rb{}; m_controller3->get_Bounds(&rb);
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] bounds=(%ld,%ld)-(%ld,%ld) get=(%ld,%ld)-(%ld,%ld)",
                  bounds.left, bounds.top, bounds.right, bounds.bottom,
                  rb.left, rb.top, rb.right, rb.bottom);
    dlog(buf);
}

HWND BrowserWnd::FindPageWnd() const
{
    if (!m_hwnd) return nullptr;
    HWND page = FirstChromeChild(m_hwnd, m_videoWnd);
    while (page)
    {
        HWND deeper = FirstChromeChild(page, m_videoWnd);
        if (!deeper) break;
        page = deeper;
    }
    return page;
}

void BrowserWnd::LayoutVideoWnd()
{
    if (!m_hwnd || !m_videoWnd) return;

    // Both modes fill the whole area; what differs is the parent and the
    // stacking, because a child HWND is only ordered against its own siblings.
    //
    //   Composition - the page is a DComp visual, composited above *every* child
    //                 HWND of this window, so VideoWnd is visible only where the
    //                 HTML punches its transparent circle. Remaining a child of
    //                 ours and filling the client area is enough.
    //
    //   Windowed    - the page is an opaque child HWND, so the only way the GIF
    //                 can show is for the compositor to blend the two windows
    //                 using the page's alpha. That needs VideoWnd to be an actual
    //                 sibling of the window that paints the page. The controller
    //                 nests that window below us:
    //
    //                   BrowserWnd
    //                     Chrome_WidgetWin_0          <- wrapper, our child
    //                       Chrome_WidgetWin_1        <- paints the page
    //                         Chrome_RenderWidgetHostHWND
    //                         Intermediate D3D Window
    //
    //                 Re-parent VideoWnd into Chrome_WidgetWin_1 so it is a real
    //                 sibling of the render windows, then drop it to the bottom
    //                 of that sibling order - underneath the page.
    HWND target = m_hwnd;
    if (m_mode == Mode::Windowed)
    {
        if (HWND page = FindPageWnd()) target = page;
    }

    const HWND wasParent = GetParent(m_videoWnd);
    const bool reparented = (wasParent != target);
    if (reparented)
    {
        // SetParent keeps the screen position, so the geometry below is what
        // actually lands the window.
        SetParent(m_videoWnd, target);
    }

    // Size to *our* client area, not to the target's own client rect. Queried
    // mid-init, Chrome_WidgetWin_1 reported a client of 1608x1529 and VideoWnd
    // came out that size, poking far outside the window; Chromium only settles
    // it to the real 1078x700 afterwards. Our client area is what both windows
    // end up covering, and it never lies. The origin is (0,0) either way:
    // Chrome_WidgetWin_1 is frameless and sits at our client origin.
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    const int w = std::max<LONG>(1, rc.right - rc.left);
    const int h = std::max<LONG>(1, rc.bottom - rc.top);

    SetWindowPos(m_videoWnd,
                 m_mode == Mode::Composition ? HWND_TOP : HWND_BOTTOM,
                 0, 0, w, h,
                 SWP_NOACTIVATE);

    wchar_t buf[192];
    std::swprintf(buf, 192,
                  L"[BrowserWnd] VideoWnd layout mode=%s parent=%p(%s) rect=0,0,%dx%d settled=%d",
                  m_mode == Mode::Composition ? L"composition" : L"windowed",
                  (void*)GetParent(m_videoWnd), reparented ? L"reparented" : L"same", w, h,
                  VideoStackSettled() ? 1 : 0);
    dlog(buf);
}

bool BrowserWnd::VideoStackSettled() const
{
    if (m_mode != Mode::Windowed || !m_videoWnd) return true;

    HWND parent = GetParent(m_videoWnd);
    if (!parent || parent == m_hwnd) return false;   // not re-parented yet

    // The page is presented through Chromium's own D3D child, not through the
    // render widget host, and that child is created late. Until it exists there
    // is nothing meaningful to sit under, and "bottom-most" would be true for
    // the wrong reason.
    bool sawPresenter = false;
    HWND last = nullptr;
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
    {
        wchar_t cls[128]{};
        if (GetClassNameW(c, cls, 128) > 0 &&
            std::wstring(cls) == L"Intermediate D3D Window")
        {
            sawPresenter = true;
        }
        last = c;   // GW_HWNDNEXT walks down the z-order, so this ends bottom-most
    }
    return sawPresenter && last == m_videoWnd;
}

void BrowserWnd::ArmVideoStackWatch()
{
    if (!m_hwnd || m_mode != Mode::Windowed) return;
    m_videoStackTicksLeft = kVideoStackWatchTicks;
    SetTimer(m_hwnd, kVideoStackWatchTimer, 250, nullptr);
}

void BrowserWnd::AttachRootVisual(bool attach)
{
    if (!m_dcompTarget) return;

    // Detaching matters when leaving composition mode. The visual is composited
    // above *every* child HWND of this window, and closing the composition
    // controller does not empty it: left attached it goes on showing the last
    // frame it was given. That frozen copy of the page then sits on top of the
    // windowed controller's own HWND, which reads exactly like the page failing
    // to render, or - worse, because it looks convincing - like the windowed
    // controller honouring the page's alpha when it is really showing a stale
    // composition.
    HRESULT hr = m_dcompTarget->SetRoot(attach ? m_rootVisual.Get() : nullptr);
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] SetRoot(%s) hr=0x%08lx",
                  attach ? L"visual" : L"null", hr);
    dlog(buf);
    if (m_dcompDevice) m_dcompDevice->Commit();
}

HRESULT BrowserWnd::InitDComp()
{
    wchar_t buf[128];
    HRESULT hr = DCompositionCreateDevice(
        nullptr, IID_PPV_ARGS(&m_dcompDevice));
    std::swprintf(buf, 128, L"[BrowserWnd] DCompositionCreateDevice hr=0x%08lx", hr);
    dlog(buf);
    if (FAILED(hr)) return hr;

    hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
    std::swprintf(buf, 128, L"[BrowserWnd] CreateTargetForHwnd hr=0x%08lx", hr);
    dlog(buf);
    if (FAILED(hr)) return hr;

    hr = m_dcompDevice->CreateVisual(&m_rootVisual);
    std::swprintf(buf, 128, L"[BrowserWnd] CreateVisual hr=0x%08lx", hr);
    dlog(buf);
    if (FAILED(hr)) return hr;

    hr = m_dcompTarget->SetRoot(m_rootVisual.Get());
    std::swprintf(buf, 128, L"[BrowserWnd] SetRoot hr=0x%08lx", hr);
    dlog(buf);
    if (FAILED(hr)) return hr;

    UpdateVisualBounds();
    hr = m_dcompDevice->Commit();
    std::swprintf(buf, 128, L"[BrowserWnd] DComp initial Commit hr=0x%08lx", hr);
    dlog(buf);
    return hr;
}

HRESULT BrowserWnd::InitWebView2()
{
    m_envHandler = Microsoft::WRL::Make<EnvCompletedHandler>(
        [this](HRESULT hr, ICoreWebView2Environment* env) {
            OnEnvCreated(hr, env);
        });
    dlog(L"[BrowserWnd] CreateCoreWebView2EnvironmentWithOptions begin");
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,               // browserExecutableFolder
        nullptr,               // userDataFolder
        nullptr,               // options
        m_envHandler.Get());
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] CreateCoreWebView2EnvironmentWithOptions hr=0x%08lx", hr);
    dlog(buf);
    return hr;
}

void BrowserWnd::OnEnvCreated(HRESULT hr, ICoreWebView2Environment* env)
{
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] OnEnvCreated hr=0x%08lx env=%p", hr, env);
    dlog(buf);
    if (FAILED(hr) || !env)
    {
        dlog(L"[BrowserWnd] WebView2 env creation failed");
        return;
    }
    m_env = env;
    if (FAILED(env->QueryInterface(IID_PPV_ARGS(&m_env3))))
    {
        dlog(L"[BrowserWnd] Environment3 not available");
        return;
    }

    // Environment is up; build whichever controller m_mode asks for. If the
    // user already flipped the switch the recorded mode wins here.
    CreateControllerForMode();
}

HRESULT BrowserWnd::CreateControllerForMode()
{
    wchar_t buf[160];
    if (!m_env) return E_UNEXPECTED;

    m_switchInFlight = true;

    if (m_mode == Mode::Composition)
    {
        if (!m_rootVisual)
        {
            dlog(L"[BrowserWnd] composition mode needs a root visual");
            m_switchInFlight = false;
            return E_UNEXPECTED;
        }
        if (!m_env3)
        {
            dlog(L"[BrowserWnd] Environment3 missing; cannot create a composition controller");
            m_switchInFlight = false;
            return E_NOINTERFACE;
        }

        // The page can only be seen through the visual, so it has to be back in
        // the tree before the controller starts painting into it.
        AttachRootVisual(true);

        m_ctrlHandler = Microsoft::WRL::Make<ControllerCompletedHandler>(
            [this](HRESULT h, ICoreWebView2CompositionController* c) {
                OnControllerCreated(h, c);
            });
        HRESULT hr = m_env3->CreateCoreWebView2CompositionController(
            m_hwnd, m_ctrlHandler.Get());
        std::swprintf(buf, 160, L"[BrowserWnd] CreateCompositionController hr=0x%08lx", hr);
        dlog(buf);
        if (FAILED(hr))
        {
            m_switchInFlight = false;
            dlog(L"[BrowserWnd] CompositionController creation failed");
        }
        return hr;
    }

    // Nothing is painted into the visual in this mode, so take it out of the
    // tree rather than leave the last composition frame frozen on top of the
    // windowed controller's HWND.
    AttachRootVisual(false);

    m_winCtrlHandler = Microsoft::WRL::Make<WindowedControllerCompletedHandler>(
        [this](HRESULT h, ICoreWebView2Controller* c) {
            OnWindowedControllerCreated(h, c);
        });
    HRESULT hr = m_env->CreateCoreWebView2Controller(m_hwnd, m_winCtrlHandler.Get());
    std::swprintf(buf, 160, L"[BrowserWnd] CreateCoreWebView2Controller hr=0x%08lx", hr);
    dlog(buf);
    if (FAILED(hr))
    {
        m_switchInFlight = false;
        dlog(L"[BrowserWnd] windowed Controller creation failed");
    }
    return hr;
}

void BrowserWnd::OnControllerCreated(HRESULT hr, ICoreWebView2CompositionController* raw)
{
    m_switchInFlight = false;
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] OnControllerCreated hr=0x%08lx raw=%p", hr, raw);
    dlog(buf);
    if (FAILED(hr) || !raw) return;

    // CompositionController inherits from Controller; QI to keep a
    // strongly-typed pointer for the rest of the API surface.
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&m_controller))))
    {
        dlog(L"[BrowserWnd] QI ICoreWebView2Controller failed");
        return;
    }
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&m_controller3))))
    {
        dlog(L"[BrowserWnd] Controller3 not available");
    }
    m_compController = raw;
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&m_controller2))))
    {
        dlog(L"[BrowserWnd] Controller2 not available");
    }

    // Hand the WebView2 our DComp visual so it paints into our tree. This is
    // the whole difference from the windowed path: alpha-blended page pixels
    // land in a visual we control, instead of in an opaque child HWND.
    HRESULT hrb = m_compController->put_RootVisualTarget(m_rootVisual.Get());
    std::swprintf(buf, 128, L"[BrowserWnd] put_RootVisualTarget hr=0x%08lx", hrb);
    dlog(buf);

    SetupController();
}

void BrowserWnd::OnWindowedControllerCreated(HRESULT hr, ICoreWebView2Controller* raw)
{
    m_switchInFlight = false;
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] OnWindowedControllerCreated hr=0x%08lx raw=%p", hr, raw);
    dlog(buf);
    if (FAILED(hr) || !raw) return;

    m_controller = raw;
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&m_controller3))))
    {
        dlog(L"[BrowserWnd] Controller3 not available");
    }
    if (FAILED(raw->QueryInterface(IID_PPV_ARGS(&m_controller2))))
    {
        dlog(L"[BrowserWnd] Controller2 not available");
    }
    // Deliberately no RootVisualTarget here: a windowed controller owns an
    // opaque child HWND and has nowhere to put a visual.

    SetupController();
}

void BrowserWnd::SetupController()
{
    wchar_t buf[192];
    const bool composition = (m_mode == Mode::Composition);

    // Bounds + visual clip. We use RAW_PIXELS so put_Bounds takes
    // physical pixels and the WebView2 renders at native resolution.
    //
    // In RAW_PIXELS mode RasterizationScale does not resize the WebView -
    // it only decides how many physical pixels one CSS pixel covers. Left
    // at the DPI-derived default (1.5 on this 150% display) the page got a
    // 719x496 CSS viewport, so every fixed px in style.css (including
    // --hole-d) landed somewhere other than the physical pixels it meant.
    //
    // ShouldDetectMonitorScaleChanges has to be turned off FIRST: while it
    // is on, WebView2 re-derives RasterizationScale from the monitor DPI
    // and silently discards our pin, which is exactly what was happening.
    //
    // Do NOT move this into UpdateWebViewBounds(): that would recurse via
    // RasterizationScaleChanged -> OnDpiChanged() -> UpdateWebViewBounds()
    // and starve the message loop (the GIF would look frozen).
    if (m_controller3)
    {
        HRESULT hrDetect = m_controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
        HRESULT hrScale = m_controller3->put_RasterizationScale(1.0f);
        double got = 0.0;
        m_controller3->get_RasterizationScale(&got);
        std::swprintf(buf, 192, L"[BrowserWnd] pin scale: detect hr=0x%08lx put hr=0x%08lx -> now %.3f",
                      (unsigned long)hrDetect, (unsigned long)hrScale, got);
        dlog(buf);
    }
    UpdateVisualBounds();
    UpdateWebViewBounds();

    // Default background. Composition mode wants a fully transparent page so
    // the cut-out reveals VideoWnd.
    //
    // EXPERIMENT: windowed mode now asks for the transparent background too, so
    // that the only variable left is whether the windowed controller's opaque
    // child HWND can carry that alpha through to the compositor.
    if (m_controller2)
    {
        COREWEBVIEW2_COLOR c{};
        c.A = 0; c.R = 0; c.G = 0; c.B = 0;
        m_controller2->put_DefaultBackgroundColor(c);
        std::swprintf(buf, 192, L"[BrowserWnd] put_DefaultBackgroundColor a=%u mode=%s",
                      (unsigned)c.A, composition ? L"composition" : L"windowed");
        dlog(buf);
    }

    // 4) Size and stack VideoWnd for this mode. Deliberately after put_Bounds()
    //    above so there is no window left at the wrong size, and before
    //    put_IsVisible() below so the GIF window is already in place when the
    //    controller first shows.
    LayoutVideoWnd();
    // Chromium has not built its presenting window yet at this point, so this
    // layout cannot be the last word; keep re-asserting until it exists.
    ArmVideoStackWatch();

    // 5) Controllers start hidden. Show explicitly so the first frame goes
    //    through.
    if (m_controller)
    {
        m_controller->put_IsVisible(TRUE);
        dlog(L"[BrowserWnd] put_IsVisible(TRUE)");
    }

    // 5) Subscribe to events. The tokens are kept so TeardownController can
    //    unhook cleanly before the controller is closed.
    m_accelHandler = Microsoft::WRL::Make<AccelHandler>(
        [this](UINT vk) { OnAcceleratorKey(vk); });
    m_controller->add_AcceleratorKeyPressed(m_accelHandler.Get(), &m_accelToken);

    m_rasterHandler = Microsoft::WRL::Make<RasterizationScaleHandler>(
        [this]() { OnDpiChanged(); });
    if (m_controller3)
    {
        m_controller3->add_RasterizationScaleChanged(m_rasterHandler.Get(), &m_rasterToken);
    }

    // 6) Pull ICoreWebView2 out and navigate.
    Microsoft::WRL::ComPtr<ICoreWebView2> webView;
    if (SUCCEEDED(m_controller->get_CoreWebView2(&webView)))
    {
        m_webView = webView;
        m_navHandler = Microsoft::WRL::Make<NavCompletedHandler>(
            [this](HRESULT status, const wchar_t*) { OnNavCompleted(status); });
        m_webView->add_NavigationCompleted(m_navHandler.Get(), &m_navToken);

        // The page tells us when a click belongs to the GIF rather than to it.
        m_msgHandler = Microsoft::WRL::Make<WebMessageHandler>(
            [this](const std::wstring& json) { OnWebMessage(json); });
        m_webView->add_WebMessageReceived(m_msgHandler.Get(), &m_msgToken);
        wchar_t urlBuf[512]; wcsncpy_s(urlBuf, m_url.c_str(), 511);
        std::swprintf(buf, 512, L"[BrowserWnd] Navigate URL=%s", urlBuf);
        dlog(buf);
        HRESULT hrb = m_webView->Navigate(m_url.c_str());
        std::swprintf(buf, 128, L"[BrowserWnd] Navigate hr=0x%08lx", hrb);
        dlog(buf);
    }

    // 7) Final commit so everything is wired and visible together.
    if (m_dcompDevice)
    {
        HRESULT hrb = m_dcompDevice->Commit();
        std::swprintf(buf, 128, L"[BrowserWnd] DComp Commit hr=0x%08lx", hrb);
        dlog(buf);
    }
}

void BrowserWnd::TeardownController()
{
    if (!m_controller) return;
    dlog(L"[BrowserWnd] TeardownController");

    // The stack it was watching is about to disappear; the next controller
    // arms its own watch.
    if (m_hwnd)
    {
        KillTimer(m_hwnd, kVideoStackWatchTimer);
        m_videoStackTicksLeft = 0;
    }

    // Detach the root visual first. Closing the composition controller does
    // not clear m_rootVisual, and an attached visual keeps the last frame
    // pinned in our tree - which would sit there as a ghost over the windowed
    // controller.
    if (m_compController && m_rootVisual)
    {
        m_compController->put_RootVisualTarget(nullptr);
    }
    if (m_webView && m_navToken.value)
    {
        m_webView->remove_NavigationCompleted(m_navToken);
    }
    if (m_webView && m_msgToken.value)
    {
        m_webView->remove_WebMessageReceived(m_msgToken);
    }
    if (m_controller3 && m_rasterToken.value)
    {
        m_controller3->remove_RasterizationScaleChanged(m_rasterToken);
    }
    if (m_accelToken.value)
    {
        m_controller->remove_AcceleratorKeyPressed(m_accelToken);
    }

    // VideoWnd may be parented into the controller's Chromium wrapper (see
    // LayoutVideoWnd). That wrapper is destroyed along with the controller and
    // would take VideoWnd - which outlives the controller - down with it, so
    // move it back under us first.
    if (m_videoWnd && GetParent(m_videoWnd) != m_hwnd)
    {
        SetParent(m_videoWnd, m_hwnd);
        dlog(L"[BrowserWnd] TeardownController: VideoWnd re-parented back under BrowserWnd");
    }

    // Close() is synchronous and tears down the child HWND / render process
    // wiring, so nothing may touch m_controller after this line.
    m_controller->Close();

    m_navToken = {};
    m_accelToken = {};
    m_rasterToken = {};
    m_msgToken = {};
    m_webView.Reset();
    m_controller.Reset();
    m_controller2.Reset();
    m_controller3.Reset();
    m_compController.Reset();
    m_accelHandler.Reset();
    m_rasterHandler.Reset();
    m_navHandler.Reset();
    m_msgHandler.Reset();
    m_ctrlHandler.Reset();
    m_winCtrlHandler.Reset();
}

void BrowserWnd::SetMode(Mode mode)
{
    if (mode == m_mode) return;

    if (!m_env)
    {
        // Environment still coming up (or not started): just record the
        // choice. OnEnvCreated builds the controller from m_mode.
        m_mode = mode;
        if (m_onModeChanged) m_onModeChanged(m_mode);
        return;
    }

    if (m_switchInFlight)
    {
        dlog(L"[BrowserWnd] SetMode ignored: a switch is already in flight");
        return;
    }

    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] SetMode -> %s",
                  mode == Mode::Composition ? L"Composition" : L"Windowed");
    dlog(buf);

    TeardownController();
    m_mode = mode;
    // Move VideoWnd straight away rather than waiting for the new controller:
    // the old one is already gone, so in windowed mode this keeps the GIF in
    // the cut-out through the rebuild instead of letting the whole client area
    // flash empty.
    LayoutVideoWnd();
    ArmVideoStackWatch();
    if (m_onModeChanged) m_onModeChanged(m_mode);
    CreateControllerForMode();
}

void BrowserWnd::ToggleMode()
{
    SetMode(m_mode == Mode::Composition ? Mode::Windowed : Mode::Composition);
}

void BrowserWnd::Resize(const RECT& bounds)
{
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr,
        bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    wchar_t buf[160];
    RECT now{}; GetClientRect(m_hwnd, &now);
    std::swprintf(buf, 160, L"[BrowserWnd] Resize bounds=(%ld,%ld)-(%ld,%ld) size=%ldx%ld (now %ldx%ld)",
                  bounds.left, bounds.top, bounds.right, bounds.bottom,
                  bounds.right - bounds.left, bounds.bottom - bounds.top,
                  now.right, now.bottom);
    dlog(buf);
    RECT wr{}; GetWindowRect(m_hwnd, &wr);
    RECT pr{}; GetClientRect(GetParent(m_hwnd), &pr);
    std::swprintf(buf, 160, L"[BrowserWnd] window=(%ld,%ld)-(%ld,%ld) parent=(%ld,%ld)-(%ld,%ld)",
                  wr.left, wr.top, wr.right, wr.bottom,
                  pr.left, pr.top, pr.right, pr.bottom);
    dlog(buf);

    // Lazy init: WebView2 needs a sized HWND to attach to. Doing this on
    // the first valid WM_SIZE avoids races where the controller is created
    // against a zero-sized target.
    InitOnceSized();

    if (m_controller3)
    {
        UpdateWebViewBounds();
    }
    if (m_rootVisual)
    {
        UpdateVisualBounds();
        m_dcompDevice->Commit();
    }

    // Last word on geometry and z-order: the cut-out moves with the client area,
    // and in windowed mode the controller may have just resized its own child
    // HWND above VideoWnd.
    LayoutVideoWnd();
}

void BrowserWnd::OnAcceleratorKey(UINT vk)
{
    // Reserved hook in case we want to handle more keys than the default
    // Tab/F10/F6/Ctrl+Esc filter inside AccelHandler. Currently the
    // handler marks every key handled.
    (void)vk;
}

void BrowserWnd::OnNavCompleted(HRESULT status)
{
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] NavCompleted status=0x%08lx", (unsigned long)status);
    dlog(buf);
    if (m_webView)
    {
        LPWSTR src = nullptr;
        if (SUCCEEDED(m_webView->get_Source(&src)))
        {
            std::wstring msg = L"[BrowserWnd] NavCompleted source=";
            msg += src ? src : L"(null)";
            dlog(msg.c_str());
            CoTaskMemFree(src);
        }
    }
}

void BrowserWnd::OnDpiChanged()
{
    if (!m_controller3 || !m_hwnd) return;
    UpdateWebViewBounds();
    if (m_rootVisual)
    {
        UpdateVisualBounds();
        m_dcompDevice->Commit();
    }
}

void BrowserWnd::OnWebMessage(const std::wstring& json)
{
    // The page fires this from the invisible element it keeps over the cut-out,
    // so it means "this click was on the GIF's area, not on any control".
    if (json.find(L"video-toggle") == std::wstring::npos) return;
    if (!m_videoWnd) return;

    static int s_toggles = 0;
    wchar_t buf[128];
    std::swprintf(buf, 128, L"[BrowserWnd] page reports a click on the GIF area (#%d)", ++s_toggles);
    dlog(buf);
    // Posted, not sent: the toggle repaints VideoWnd, and doing that inside the
    // WebView2's callback would re-enter the browser while it is dispatching.
    PostMessageW(m_videoWnd, WM_LBUTTONDOWN, 0, 0);
}

void BrowserWnd::SendMouseToWebView(UINT msg, WPARAM wParam, POINT clientPt)
{
    if (!m_compController) return;

    // SendMouseInput is the supported mouse path in composition mode, and it
    // is what the WinComp sample uses. SendPointerInput takes an
    // ICoreWebView2PointerInfo that the host must populate by hand; the
    // hand-rolled one used here left PointerFlags at zero, so Chromium could
    // not tell which button was involved and dropped the click.
    //
    // COREWEBVIEW2_MOUSE_EVENT_KIND values are deliberately the same numbers
    // as the WM_MOUSE* messages, so the cast is exact.
    UINT32 mouseData = 0;
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
        mouseData = (UINT32)(SHORT)GET_WHEEL_DELTA_WPARAM(wParam);
    else if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK)
        mouseData = GET_XBUTTON_WPARAM(wParam);

    m_compController->SendMouseInput(
        static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(msg),
        static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(wParam)),
        mouseData,
        clientPt);
}

void BrowserWnd::ForwardPointerToWebView(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!m_compController) return;

    Microsoft::WRL::ComPtr<PointerInfo> info = Microsoft::WRL::Make<PointerInfo>();

    COREWEBVIEW2_POINTER_EVENT_KIND kind = COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE;
    if (msg == WM_POINTERDOWN) kind = COREWEBVIEW2_POINTER_EVENT_KIND_DOWN;
    else if (msg == WM_POINTERUP) kind = COREWEBVIEW2_POINTER_EVENT_KIND_UP;

    UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType = PT_POINTER;
    POINTER_INFO pi{};
    if (GetPointerType(pointerId, &pointerType) && pointerType != PT_POINTER)
    {
        if (GetPointerInfo(pointerId, &pi))
        {
            info->CopyFromWin32(pi);
        }
        else
        {
            info->put_PointerKind(pointerType);
            info->put_PointerId(pointerId);
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            info->put_PixelLocation(p);
        }
    }
    else
    {
        info->put_PointerKind(PT_POINTER);
        info->put_PointerId(pointerId);
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        info->put_PixelLocation(p);
    }

    m_compController->SendPointerInput(kind, info.Get());
}

LRESULT BrowserWnd::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wp == kVideoStackWatchTimer)
        {
            // The stack is only worth re-asserting while it is still wrong;
            // once it settles, drop the timer rather than fight Chromium.
            if (m_mode == Mode::Windowed &&
                !VideoStackSettled() &&
                m_videoStackTicksLeft > 0)
            {
                --m_videoStackTicksLeft;
                LayoutVideoWnd();
            }
            else
            {
                KillTimer(h, kVideoStackWatchTimer);
                m_videoStackTicksLeft = 0;
                dlog(L"[BrowserWnd] VideoWnd stack settled; watch stopped");
            }
            return 0;
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        // Claim the whole client area and route by hand below. Returning
        // HTTRANSPARENT inside the cut-out let the OS walk the hit test down
        // to some other window instead, so the messages never reached this
        // WndProc and could not be forwarded to either target deliberately.
        return HTCLIENT;
    case WM_SIZE:
        OnDpiChanged();
        return 0;
    case WM_DPICHANGED:
        OnDpiChanged();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        ForwardPointerToWebView(msg, wp, lp);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    {
        // Mouse input. The window has no mouse-in-pointer promotion, so mouse
        // clicks arrive here as legacy messages (the WM_POINTER* branch above
        // only sees touch and pen).
        //
        // Everything goes to the page, including points inside the cut-out.
        // Deciding "page or GIF" by geometry here is what made buttons that
        // overlap the cut-out dead: the host cannot see what the page has drawn
        // at a point, so it cannot know whether the pixel under the pointer
        // belongs to a control. The page can, and says so by postMessage - see
        // OnWebMessage().
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SendMouseToWebView(msg, wp, pt);
        return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        // Wheel messages are the odd ones out: lParam carries screen
        // coordinates, not client ones.
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        SendMouseToWebView(msg, wp, pt);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        // Composition mode does not synthesize keystrokes for the WebView2,
        // so text input is best-effort. Returning 0 keeps DefWindowProc from
        // doing anything we don't want.
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK BrowserWnd::StaticWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto self = reinterpret_cast<BrowserWnd*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->WndProc(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

