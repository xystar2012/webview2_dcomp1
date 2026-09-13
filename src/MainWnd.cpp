#include "MainWnd.h"
#include "util.h"

#include <windows.h>
#include <windowsx.h>

namespace
{
    constexpr int kWindowW = 1100;
    constexpr int kWindowH = 800;
    constexpr int kMinW    = 720;
    constexpr int kMinH    = 540;
}

MainWnd::MainWnd() = default;
MainWnd::~MainWnd() = default;

bool MainWnd::Create(HINSTANCE hInst, const std::wstring& gifPath, const std::wstring& url)
{
    m_url = url;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWnd::StaticWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"webview2_dcomp1.MainWnd";
    static ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    // WS_CLIPCHILDREN prevents the parent from erasing under the children
    // during invalidation - critical for clean drags because it stops the
    // flash you get when GDI paints a default bg and DWM then composites
    // the children on top.
    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"WebView2 DComp three-layer demo",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH,
        nullptr, nullptr, hInst, this);
    if (!m_hwnd) return false;

    // BrowserWnd first so it becomes the DComp target with the full client
    // area. VideoWnd is then parented to BrowserWnd so the GIF lives on
    // BrowserWnd''s own window surface; DComp still composites the WebView2
    // visual on top, so transparent regions in the HTML reveal the GIF.
    m_browserWnd = std::make_unique<BrowserWnd>();
    RECT rc; GetClientRect(m_hwnd, &rc);
    if (!m_browserWnd->Create(m_hwnd, rc, url)) return false;

    m_videoWnd = std::make_unique<VideoWnd>();
    if (!m_videoWnd->Create(m_browserWnd->GetHwnd(), gifPath.c_str())) return false;
    m_browserWnd->SetVideoWnd(m_videoWnd->GetHwnd());

    return true;
}

void MainWnd::OnSize()
{
    if (!m_hwnd) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    if (m_browserWnd)
    {
        m_browserWnd->Resize(rc);
    }
    if (m_videoWnd)
    {
        // VideoWnd fills BrowserWnd''s client area (which equals MainWnd''s
        // client area because BrowserWnd is a full-bleed child).
        SetWindowPos(m_videoWnd->GetHwnd(), nullptr,
            0, 0, rc.right - rc.left, rc.bottom - rc.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWnd::OnGetMinMaxInfo(LPMINMAXINFO mmi)
{
    mmi->ptMinTrackSize = { kMinW, kMinH };
}

LRESULT MainWnd::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_GETMINMAXINFO:
        OnGetMinMaxInfo(reinterpret_cast<LPMINMAXINFO>(lp));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK MainWnd::StaticWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto self = reinterpret_cast<MainWnd*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->WndProc(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}
