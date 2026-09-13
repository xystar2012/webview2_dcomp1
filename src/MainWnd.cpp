#include "MainWnd.h"
#include "util.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>

namespace
{
    constexpr int kWindowW = 1100;
    constexpr int kWindowH = 800;
    constexpr int kMinW    = 720;
    constexpr int kMinH    = 540;

    // Control ids for the strip.
    constexpr int kIdModeButton = 1001;
    constexpr int kIdModeHint   = 1002;

    constexpr COLORREF kStripBg   = RGB(24, 24, 28);
    constexpr COLORREF kStripLine = RGB(64, 64, 72);
    constexpr COLORREF kHintText  = RGB(170, 170, 180);

    // Font for the strip controls, sized from the shell's message font so it
    // matches the rest of the OS chrome instead of the ancient system bitmap
    // font a stock BUTTON gets by default.
    HFONT CreateUiFont(UINT dpi)
    {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            return nullptr;

        LOGFONTW lf = ncm.lfMessageFont;
        if (dpi != 0 && dpi != 96)
        {
            lf.lfHeight = MulDiv(lf.lfHeight, (int)dpi, 96);
        }
        return CreateFontIndirectW(&lf);
    }
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

    // BrowserWnd first so it becomes the DComp target. It covers the client
    // area minus the bottom control strip. VideoWnd is then parented to
    // BrowserWnd so the GIF lives on BrowserWnd's own window surface; DComp
    // still composites the WebView2 visual on top, so transparent regions in
    // the HTML reveal the GIF.
    m_browserWnd = std::make_unique<BrowserWnd>();
    if (!m_browserWnd->Create(m_hwnd, BrowserRect(), url)) return false;

    m_videoWnd = std::make_unique<VideoWnd>();
    if (!m_videoWnd->Create(m_browserWnd->GetHwnd(), gifPath.c_str())) return false;
    m_browserWnd->SetVideoWnd(m_videoWnd->GetHwnd());

    // The mode switch lives in the strip, below BrowserWnd's rectangle. It
    // must not be parented inside that rectangle: a DComp visual composites
    // above the window's child HWNDs, so the button would be invisible behind
    // the page in composition mode.
    m_uiFont = CreateUiFont(GetDpiForWindow(m_hwnd));

    m_modeButton = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 10, 10, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdModeButton)),
        hInst, nullptr);
    if (!m_modeButton) return false;

    m_modeHint = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdModeHint)),
        hInst, nullptr);
    if (!m_modeHint) return false;

    if (m_uiFont)
    {
        SendMessageW(m_modeButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        SendMessageW(m_modeHint,   WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    }

    // Keep the button label in step with what BrowserWnd actually built.
    m_browserWnd->SetModeChangedCallback([this](BrowserWnd::Mode) { SyncModeButton(); });
    SyncModeButton();

    OnSize();
    return true;
}

RECT MainWnd::BrowserRect() const
{
    RECT rc{};
    if (m_hwnd) GetClientRect(m_hwnd, &rc);
    rc.bottom = std::max<LONG>(rc.top + 1, rc.bottom - kStripHeight);
    return rc;
}

void MainWnd::OnSize()
{
    if (!m_hwnd) return;

    const RECT browser = BrowserRect();
    if (m_browserWnd)
    {
        // BrowserWnd::Resize also lays VideoWnd out: its geometry depends on
        // the WebView2 mode, so it belongs next to the controller rather than
        // here.
        m_browserWnd->Resize(browser);
    }

    if (m_modeButton)
    {
        const int btnW = 300;
        const int btnH = 28;
        const int bx = 12;
        const int by = browser.bottom + (kStripHeight - btnH) / 2;
        SetWindowPos(m_modeButton, HWND_TOP, bx, by, btnW, btnH,
                     SWP_NOACTIVATE);
    }
    if (m_modeHint)
    {
        RECT cr{}; GetClientRect(m_hwnd, &cr);
        const int hx = 12 + 300 + 16;
        const int hy = browser.bottom;
        SetWindowPos(m_modeHint, HWND_TOP, hx, hy,
                     std::max<LONG>(1, cr.right - hx - 12), kStripHeight,
                     SWP_NOACTIVATE);
    }

    // Repaint the strip: growing the window exposes new pixels there and
    // nothing else paints them.
    InvalidateRect(m_hwnd, &browser, FALSE);
    RECT strip{ 0, browser.bottom, 0, 0 };
    GetClientRect(m_hwnd, &strip);
    strip.top = browser.bottom;
    InvalidateRect(m_hwnd, &strip, FALSE);
}

void MainWnd::OnPaint()
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(m_hwnd, &ps);

    RECT cr{}; GetClientRect(m_hwnd, &cr);
    HBRUSH bg = CreateSolidBrush(kStripBg);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);

    // Hairline between the page and the strip.
    RECT line = { 0, cr.bottom - kStripHeight - 1, cr.right, cr.bottom - kStripHeight };
    if (line.top >= 0 && line.top < cr.bottom)
    {
        HBRUSH lb = CreateSolidBrush(kStripLine);
        FillRect(dc, &line, lb);
        DeleteObject(lb);
    }

    EndPaint(m_hwnd, &ps);
}

void MainWnd::OnModeButton()
{
    if (m_browserWnd) m_browserWnd->ToggleMode();
    SyncModeButton();
}

void MainWnd::SyncModeButton()
{
    if (!m_modeButton || !m_browserWnd) return;
    const bool composition = (m_browserWnd->GetMode() == BrowserWnd::Mode::Composition);

    SetWindowTextW(m_modeButton, composition
        ? L"WebView2 mode: Composition (DComp)  \x25B8  switch to Windowed"
        : L"WebView2 mode: Windowed (HWND)     \x25B8  switch to Composition");

    if (m_modeHint)
    {
        SetWindowTextW(m_modeHint, composition
            ? L"Page is alpha-blended over the GIF; transparent pixels show the native layer through."
            : L"Controller owns an opaque child HWND with no per-pixel alpha; the GIF is a native window clipped to the cut-out on top of it.");
        InvalidateRect(m_modeHint, nullptr, TRUE);
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
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        // The strip is painted in WM_PAINT; erasing here as well would just
        // flash during a live resize drag.
        return 1;
    case WM_CTLCOLORSTATIC:
    {
        // The hint label sits on the strip, so give it the strip's background
        // and text colour instead of the default button-face grey.
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kHintText);
        static HBRUSH s_stripBrush = CreateSolidBrush(kStripBg);
        return reinterpret_cast<LRESULT>(s_stripBrush);
    }
    case WM_COMMAND:
        if (LOWORD(wp) == kIdModeButton && HIWORD(wp) == BN_CLICKED)
        {
            OnModeButton();
            return 0;
        }
        break;
    case WM_DPICHANGED:
    {
        // Rebuild the strip font at the new scale, then let DefWindowProc
        // apply the suggested rectangle so the window resizes correctly.
        if (m_uiFont) { DeleteObject(m_uiFont); m_uiFont = nullptr; }
        m_uiFont = CreateUiFont(HIWORD(wp));
        if (m_uiFont)
        {
            SendMessageW(m_modeButton, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
            SendMessageW(m_modeHint,   WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }
        break;
    }
    case WM_DESTROY:
        if (m_uiFont) { DeleteObject(m_uiFont); m_uiFont = nullptr; }
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
