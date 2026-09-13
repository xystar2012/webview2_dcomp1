#pragma once

#include "VideoWnd.h"
#include "BrowserWnd.h"

#include <memory>
#include <string>

// Top-level window that owns the two siblings.
//   - VideoWnd  : created first, sits at the bottom of the z-stack
//   - BrowserWnd: created second, sits on top of VideoWnd
// The composition chain itself lives inside BrowserWnd; VideoWnd is a plain
// D2D HWND that just needs to be below BrowserWnd in z-order.
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

    HWND m_hwnd = nullptr;
    std::unique_ptr<VideoWnd> m_videoWnd;
    std::unique_ptr<BrowserWnd> m_browserWnd;
    std::wstring m_url;
};
