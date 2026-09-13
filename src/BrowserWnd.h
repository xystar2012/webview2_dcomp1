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
//                 punch a hole with, so VideoWnd is re-parented in beside the
//                 window that paints the page and pushed to the bottom of that
//                 order; the compositor then blends the page's alpha over it.
//                 Kept switchable because the two paths differ in how they
//                 behave during a live resize drag.
//
// Pointer routing:
//   - In composition mode WM_NCHITTEST claims the whole client area (HTCLIENT),
//     so every mouse message lands in this WndProc and is handed to the
//     WebView2 through SendMouseInput. In windowed mode the page's own child
//     HWND takes the mouse directly and none of this runs.
//   - Nothing is routed by geometry. Only the page knows whether a point is
//     over a button or over empty space, so the page carries an invisible
//     element over the cut-out and postMessage()s us when a click lands there.
//     That is what toggles the GIF, via OnWebMessage().
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

    // Size, parent and stack VideoWnd for the current mode. See the definition
    // for why the two modes need different geometry to produce the same picture.
    void LayoutVideoWnd();

    // The window the windowed controller actually paints the page into. The
    // controller nests its windows below us, so this is the innermost
    // Chrome_WidgetWin_* descendant: BrowserWnd > Chrome_WidgetWin_0 >
    // Chrome_WidgetWin_1. VideoWnd is re-parented into it so the page and the
    // GIF are siblings and their z-order is ours to set. Null before the
    // controller is up, or in composition mode.
    HWND FindPageWnd() const;

    // True once the windowed stack needs no further attention: the window
    // Chromium presents the page through exists, and VideoWnd is the bottom-most
    // child of the page window (so the page draws over it). False while the
    // stack is still missing or ordered wrong. Feeds the layout log line - it is
    // the readout for whether the stack is where it should be.
    bool VideoStackSettled() const;

    HRESULT InitDComp();
    HRESULT InitWebView2();

    // Attach or detach the DComp visual tree from the composition target.
    // Detaching is what keeps a stale composition frame from sitting on top of
    // the windowed controller's HWND.
    void AttachRootVisual(bool attach);

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


    // Handle window.chrome.webview.postMessage() from the page. The page is the
    // only side that knows what is actually under a point, so it - not the host
    // - decides whether a click inside the cut-out belongs to a page control or
    // to the GIF, and tells us when it is the GIF's turn.
    void OnWebMessage(const std::wstring& json);

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
    Microsoft::WRL::ComPtr<WebMessageHandler> m_msgHandler;

    // Kept so the handler can be detached before the controller goes away.
    EventRegistrationToken m_navToken{};
    EventRegistrationToken m_accelToken{};
    EventRegistrationToken m_rasterToken{};
    EventRegistrationToken m_msgToken{};
};
