#include "VideoWnd.h"
#include "util.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT_PTR kTimerId = 1;
    constexpr int     kTimerMs = 16;        // ~60Hz cadence

    void dlog(const wchar_t* msg)
    {
        wchar_t path[MAX_PATH];
        DWORD n = GetTempPathW(MAX_PATH, path);
        if (n == 0 || n >= MAX_PATH) return;
        std::wstring p(path, n);
        p += L"webview2_dcomp1_debug.log";
        std::ofstream f(p, std::ios::app | std::ios::binary);
        const wchar_t bom = 0xFEFF;
        f.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
        std::wstring line = std::wstring(msg) + L"\n";
        f.write(reinterpret_cast<const char*>(line.c_str()),
                std::streamsize(line.size() * sizeof(wchar_t)));
        OutputDebugStringW(msg);
    }
}

VideoWnd::VideoWnd() = default;
VideoWnd::~VideoWnd() = default;

bool VideoWnd::Create(HWND parent, const wchar_t* gifPath)
{
    if (gifPath) m_gifPath = gifPath;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &VideoWnd::StaticWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"webview2_dcomp1.VideoWnd";
    static ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    m_hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Video",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 100, 100, parent, nullptr, wc.hInstance, this);
    if (!m_hwnd) return false;

    if (!CreateD2D()) return false;

    bool loaded = m_animator.Load(m_rt.Get(), m_gifPath.c_str());
    {
        wchar_t gifBuf[320];
        std::swprintf(gifBuf, 320, L"[VideoWnd] Load RET=%d frames=%zu path=%s",
                      (int)loaded, m_animator.FrameCount(), m_gifPath.c_str());
        dlog(gifBuf);
    }

    SetTimer(m_hwnd, kTimerId, kTimerMs, nullptr);
    return true;
}

bool VideoWnd::CreateD2D()
{
    if (!m_d2dFactory)
    {
        HRESULT hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            IID_PPV_ARGS(&m_d2dFactory));
        if (FAILED(hr)) return false;
    }

    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(
        std::max<LONG>(1, rc.right - rc.left),
        std::max<LONG>(1, rc.bottom - rc.top));

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    HRESULT hr = m_d2dFactory->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(m_hwnd, sz),
        &m_rt);
    if (FAILED(hr)) return false;

    hr = m_rt->CreateSolidColorBrush(
        D2D1::ColorF(0.10f, 0.10f, 0.12f, 1.0f), &m_brush);
    return SUCCEEDED(hr);
}

void VideoWnd::OnSize(UINT w, UINT h)
{
    if (m_rt)
    {
        m_rt->Resize(D2D1::SizeU(std::max<LONG>(1, (LONG)w), std::max<LONG>(1, (LONG)h)));
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void VideoWnd::OnPaint()
{
    if (!m_rt) return;
    ++m_paintCount;

    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(0.05f, 0.05f, 0.08f, 1.0f));

    if (ID2D1Bitmap* bmp = m_animator.CurrentBitmap())
    {
        // Drawn 1:1 and centred. The WebView2 above us cuts a hole exactly the
        // GIF's own size at the same spot (BrowserWnd::VideoRectPx and
        // --video-w / --video-h in style.css), so the picture lands inside the
        // cut-out pixel for pixel - no scaling, no cropping.
        //
        // floor() keeps the blit on whole pixels; half-pixel offsets would
        // resample a 1:1 copy and soften it.
        D2D1_SIZE_F target = m_rt->GetSize();
        D2D1_SIZE_F src = bmp->GetSize();
        if (src.width > 0 && src.height > 0)
        {
            D2D1_POINT_2F origin = {
                std::floor((target.width  - src.width)  * 0.5f),
                std::floor((target.height - src.height) * 0.5f) };
            D2D1_RECT_F rect = {
                origin.x, origin.y,
                origin.x + src.width, origin.y + src.height };
            m_rt->DrawBitmap(bmp, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

            // Frozen-frame badge, so a paused GIF is not mistaken for a slow
            // one. Drawn on the picture's own top-left corner.
            if (m_animator.IsPaused()) DrawPausedBadge(origin, rect);
        }
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        m_rt.Reset();
        m_brush.Reset();
        CreateD2D();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void VideoWnd::DrawPausedBadge(D2D1_POINT_2F origin, const D2D1_RECT_F& pic)
{
    constexpr float kInset = 14.0f;   // gap between the badge and the picture edge
    constexpr float kBox   = 46.0f;   // badge side
    constexpr float kBarW  = 8.0f;    // one pause bar
    constexpr float kBarH  = 22.0f;

    D2D1_POINT_2F c = { origin.x + kInset, origin.y + kInset };
    D2D1_RECT_F box = { c.x, c.y, c.x + kBox, c.y + kBox };
    if (box.right > pic.right || box.bottom > pic.bottom) return;  // picture too small

    m_brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
    m_rt->FillRectangle(box, m_brush.Get());

    m_brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
    float barTop = c.y + (kBox - kBarH) * 0.5f;
    float gap    = 6.0f;
    float left   = c.x + (kBox - (kBarW * 2.0f + gap)) * 0.5f;
    m_rt->FillRectangle(D2D1::RectF(left, barTop, left + kBarW, barTop + kBarH), m_brush.Get());
    m_rt->FillRectangle(D2D1::RectF(left + kBarW + gap, barTop,
                                     left + kBarW * 2.0f + gap, barTop + kBarH), m_brush.Get());
}

void VideoWnd::OnTimer()
{
    m_animator.Advance((DWORD)kTimerMs);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    static DWORD s_ticks = 0;
    if ((++s_ticks % 30) == 0)
    {
        wchar_t buf[160];
        std::swprintf(buf, 160, L"[VideoWnd] timer ticks=%lu paints=%lu idx=%zu of %zu",
                      (unsigned long)s_ticks,
                      m_paintCount,
                      m_animator.CurrentIndex(),
                      m_animator.FrameCount());
        dlog(buf);
    }
}

void VideoWnd::OnLButtonDown()
{
    // A click anywhere in the cut-out freezes the GIF; the next one resumes it.
    // BrowserWnd routes these clicks here from its WM_NCHITTEST handler, so a
    // click that lands on the picture never reaches the WebView2.
    m_animator.TogglePaused();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT VideoWnd::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        OnPaint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_TIMER:
        OnTimer();
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown();
        return 0;
    case WM_NCHITTEST:
    {
        // Two shapes, two answers.
        //
        // In composition mode BrowserWnd stretches this window across its whole
        // client area and lets the page's alpha reveal the picture, so the window
        // must stay transparent to hit testing - otherwise it would swallow every
        // mouse message before BrowserWnd could route it.
        //
        // In windowed mode there is no alpha to lean on and BrowserWnd shrinks the
        // window to the cut-out rectangle instead (see LayoutVideoWnd). Its client
        // area is then no bigger than the picture itself, so HTCLIENT can only ever
        // claim clicks that already belong to the GIF - which is what the pause
        // toggle wants.
        RECT rc{};
        GetClientRect(h, &rc);
        const bool clampedToPicture =
            m_animator.IsLoaded() &&
            (rc.right - rc.left) == static_cast<LONG>(m_animator.Width()) &&
            (rc.bottom - rc.top) == static_cast<LONG>(m_animator.Height());
        return clampedToPicture ? HTCLIENT : HTTRANSPARENT;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK VideoWnd::StaticWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto self = reinterpret_cast<VideoWnd*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->WndProc(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

void VideoWnd::NotifyFrameChanged() {}