#pragma once

#include "PointerInfo.h"

#include <wrl/client.h>
#include <wrl/implements.h>
#include <dcomp.h>
#include <WebView2.h>

#include <functional>
#include <string>

#include "BrowserWndHandlers.h"

// Top layer of the three-layer stack: hosts the WebView2, in one of two modes.
//
//   Composition - the WebView2 paints into a DirectComposition visual owned by
//                 this window. The page is alpha-blended over the window's own
//                 surface, so the transparent cut-out in the HTML reveals
//                 VideoWnd drawing underneath. This is the only mode that can
//                 show the GIF through the page.
//
//   Windowed    - the classic ICoreWebView2Controller, which creates an opaque
//                 child HWND of this window. There is no per-pixel alpha to
//                 punch a hole with, so instead VideoWnd is shrunk to the
//                 cut-out rectangle and kept above that HWND: the GIF is drawn
//                 by a real native window sitting on top of the page. Kept
//                 switchable because the two paths differ in how they behave
//                 during a live resize drag.
//
// Pointer routing (all decided here, not by the OS):
//   - WM_NCHITTEST claims the whole client area (HTCLIENT), so every mouse
//     message lands in this WndProc.
//   - A point inside the cut-out rectangle is forwarded to VideoWnd, which
//     draws the GIF showing through it.
//   - Anywhere else it goes to the WebView2 via
//     ICoreWebView2CompositionController::SendMouseInput. In windowed mode the
//     WebView2's own child HWND sits on top and takes the mouse directly, so
//     this hand-rolled routing only applies to the composition path.
class BrowserWnd
{
public:
    enum class Mode { Composition, Windowed };

    BrowserWnd() = default;
    ~BrowserWnd();

    bool Create(HWND parent, const RECT& bounds, const std::wstring& url);
    HWND GetHwnd() const { return m_hwnd; }

    // Set by MainWnd after VideoWnd has been parented to us. Pointer events
    // in the transparent disc are forwarded to this HWND.
    void SetVideoWnd(HWND hwnd) { m_videoWnd = hwnd; }

    // Rebuild the WebView2 in the other hosting mode. The controller is torn
    // down and recreated, so the page reloads from m_url and any page state is
    // lost. Ignored while a previous switch is still in flight.
    void SetMode(Mode mode);
    void ToggleMode();
    Mode GetMode() const { return m_mode; }

    // Fired on the UI thread as soon as the mode is recorded, which is before
    // the new controller has finished coming up.
    void SetModeChangedCallback(std::function<void(Mode)> cb)
    {
        m_onModeChanged = std::move(cb);
    }

    // Called by MainWnd when the top-level window is resized so we can
    // keep DComp and the WebView2 bounds in sync. Triggers lazy init of
    // DComp + WebView2 on the first non-zero size.
    void Resize(const RECT& bounds);

    // Updated when DPI changes - forwards the new DPI to WebView2.
    void OnDpiChanged();
    void OnAcceleratorKey(UINT vk);
    void OnNavCompleted(HRESULT status);

private:
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);

    void InitOnceSized();
    void UpdateVisualBounds();
    void UpdateWebViewBounds();

    // Size and stack VideoWnd for the current mode. See the definition for why
    // the two modes need different geometry to produce the same picture.
    void LayoutVideoWnd();

    HRESULT InitDComp();
    HRESULT InitWebView2();

    // Async-completion callbacks from WebView2.
    void OnEnvCreated(HRESULT hr, ICoreWebView2Environment* env);
    void OnControllerCreated(HRESULT hr, ICoreWebView2CompositionController* raw);
    void OnWindowedControllerCreated(HRESULT hr, ICoreWebView2Controller* raw);

    // Shared tail of both controller-creation paths: pin the scale, set bounds,
    // background, visibility, hooks and the initial navigation.
    void SetupController();

    // Kick off controller creation for the current m_mode.
    HRESULT CreateControllerForMode();

    // Close and release the current controller plus everything hanging off it.
    void TeardownController();


    // The transparent cut-out the GIF shows through, in client pixels. Used by
    // WM_NCHITTEST to decide whether a point belongs to the page or to the
    // native video window underneath.
    RECT VideoRectPx() const;
    bool IsInVideoRect(POINT pt) const;

    bool m_initialized = false;

    // Forward a Win32 pointer message (touch/pen) to the WebView2.
    void ForwardPointerToWebView(UINT msg, WPARAM wParam, LPARAM lParam);

    // Forward a legacy mouse message to the WebView2 with SendMouseInput.
    // clientPt is in this window's client coordinates, which are also
    // WebView2-local because the controller bounds start at (0,0).
    void SendMouseToWebView(UINT msg, WPARAM wParam, POINT clientPt);

    HWND m_hwnd = nullptr;
    HWND m_videoWnd = nullptr;
    std::wstring m_url;

    Mode m_mode = Mode::Composition;
    // True between CreateControllerForMode() and its completion handler, so a
    // second click cannot tear down a controller that is still being built.
    bool m_switchInFlight = false;
    std::function<void(Mode)> m_onModeChanged;

    // The cut-out is the GIF's own rectangle, centred in the client area, and
    // VideoWnd draws the GIF 1:1 inside it. Sizes are in CSS pixels: the
    // WebView2 rasterization scale is pinned to 1.0 (see OnControllerCreated)
    // so CSS pixels and physical pixels are 1:1, and these map onto
    // --video-w / --video-h in style.css. Keep the two in sync.
    static constexpr double kVideoWidthCss  = 648.0;
    static constexpr double kVideoHeightCss = 338.0;

    // DComp
    Microsoft::WRL::ComPtr<IDCompositionDevice> m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual> m_rootVisual;

    // WebView2
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment3> m_env3;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller3> m_controller3;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> m_controller2;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> m_compController;

    Microsoft::WRL::ComPtr<AccelHandler> m_accelHandler;
    Microsoft::WRL::ComPtr<RasterizationScaleHandler> m_rasterHandler;
    Microsoft::WRL::ComPtr<NavCompletedHandler> m_navHandler;
    Microsoft::WRL::ComPtr<EnvCompletedHandler> m_envHandler;
    Microsoft::WRL::ComPtr<ControllerCompletedHandler> m_ctrlHandler;
    Microsoft::WRL::ComPtr<WindowedControllerCompletedHandler> m_winCtrlHandler;

    // Kept so the handler can be detached before the controller goes away.
    EventRegistrationToken m_navToken{};
    EventRegistrationToken m_accelToken{};
    EventRegistrationToken m_rasterToken{};
};
