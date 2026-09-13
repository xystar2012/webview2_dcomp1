#pragma once

#include "GifAnimator.h"

#include <wrl/client.h>
#include <d2d1.h>

#include <string>

// Bottom layer of the three-layer stack: renders an animated GIF on a
// direct2d HwndRenderTarget. The middle (BrowserWnd) is on top with a
// transparent hole in the middle so this GIF shows through.
class VideoWnd
{
public:
    VideoWnd();
    ~VideoWnd();

    bool Create(HWND parent, const wchar_t* gifPath);
    HWND GetHwnd() const { return m_hwnd; }

    // Pumped by the host when the GIF has a new frame so we can keep the
    // timer cadence independent of WM_PAINT.
    void NotifyFrameChanged();

private:
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);

    bool CreateD2D();
    void OnPaint();
    void OnSize(UINT w, UINT h);
    void OnTimer();
    void OnLButtonDown();
    // Pause glyph drawn over the picture while playback is frozen.
    void DrawPausedBadge(D2D1_POINT_2F origin, const D2D1_RECT_F& pic);

    HWND m_hwnd = nullptr;
    std::wstring m_gifPath;
    GifAnimator m_animator;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_rt;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;

    // Diagnostics: number of WM_PAINTs actually serviced. Logged next to the
    // timer count so a frozen GIF can be told apart from a starved message
    // loop (ticks advancing but paints stuck is the starved case).
    unsigned long m_paintCount = 0;
};
