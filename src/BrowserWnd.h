#pragma once

#include "PointerInfo.h"

#include <wrl/client.h>
#include <wrl/implements.h>
#include <dcomp.h>
#include <WebView2.h>

#include <string>

#include "BrowserWndHandlers.h"

// Top layer of the three-layer stack: hosts the WebView2 in DirectComposition
// mode. The root DComp visual is what the WebView2 paints into; transparency
// inside the page (the disc) lets VideoWnd below show through.
//
// Pointer routing (all decided here, not by the OS):
//   - WM_NCHITTEST claims the whole client area (HTCLIENT), so every mouse
//     message lands in this WndProc.
//   - A point inside the cut-out rectangle is forwarded to VideoWnd, which
//     draws the GIF showing through it.
//   - Anywhere else it goes to the WebView2 via
//     ICoreWebView2CompositionController::SendMouseInput.
class BrowserWnd
{
public:
    BrowserWnd() = default;
    ~BrowserWnd();

    bool Create(HWND parent, const RECT& bounds, const std::wstring& url);
    HWND GetHwnd() const { return m_hwnd; }

    // Set by MainWnd after VideoWnd has been parented to us. Pointer events
    // in the transparent disc are forwarded to this HWND.
    void SetVideoWnd(HWND hwnd) { m_videoWnd = hwnd; }

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

    HRESULT InitDComp();
    HRESULT InitWebView2();

    // Async-completion callbacks from WebView2.
    void OnEnvCreated(HRESULT hr, ICoreWebView2Environment* env);
    void OnControllerCreated(HRESULT hr, ICoreWebView2CompositionController* raw);


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
};
