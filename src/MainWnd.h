#pragma once

#include "VideoWnd.h"
#include "BrowserWnd.h"

#include <memory>
#include <string>

// Top-level window that owns the two siblings.
//   - BrowserWnd: hosts the WebView2, occupies everything above the strip
//   - VideoWnd  : parented to BrowserWnd, draws the GIF underneath the page
//
// The bottom kStripHeight pixels of the client area are deliberately left
// uncovered by BrowserWnd and hold plain Win32 controls. That is not cosmetic:
// a DirectComposition visual is composited above this window's child HWNDs, so
// anything parented to MainWnd inside BrowserWnd's rectangle would be hidden
// behind the page in composition mode. The strip is the one place a native
// control stays both visible and clickable in either WebView2 mode.
class MainWnd
{
public:
    MainWnd();
    ~MainWnd();

    bool Create(HINSTANCE hInst,
                const std::wstring& gifPath,
                const std::wstring& url);
    HWND GetHwnd() const { return m_hwnd; }

private:
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);

    void OnSize();
    void OnGetMinMaxInfo(LPMINMAXINFO);
    void OnPaint();
    void OnModeButton();
    void SyncModeButton();

    // Client rect minus the bottom strip: the area BrowserWnd owns.
    RECT BrowserRect() const;

    // Height of the control strip pinned to the bottom of the client area,
    // in physical pixels.
    static constexpr int kStripHeight = 44;

    HWND m_hwnd = nullptr;
    HWND m_modeButton = nullptr;
    HWND m_modeHint = nullptr;
    HFONT m_uiFont = nullptr;
    std::unique_ptr<VideoWnd> m_videoWnd;
    std::unique_ptr<BrowserWnd> m_browserWnd;
    std::wstring m_url;
};
