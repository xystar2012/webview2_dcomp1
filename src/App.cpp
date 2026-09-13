#include "App.h"
#include "util.h"

#include <windows.h>

#include <stdexcept>
#include <string>

App::App() = default;
App::~App() = default;

int App::Run(HINSTANCE hInst, int nCmdShow)
{
    util::set_dpi_awareness();
    util::ComInit com;

    std::wstring exe = util::exe_dir();
    std::wstring html = util::join(exe, L"html\\index.html");
    std::wstring gif  = util::join(exe, L"marketplace.gif");

    // Build a file:// URL manually. shlwapi's PathToFileUrlW was dropped
    // from recent Windows SDKs and we only need a minimal converter that
    // handles backslashes, drive letters, and a few common reserved chars.
    std::wstring url = L"file:///";
    for (wchar_t c : html)
    {
        if      (c == L'\\') url.push_back(L'/');
        else if (c == L' ')  url += L"%20";
        else if (c == L'#')  url += L"%23";
        else if (c == L'?')  url += L"%3F";
        else                 url.push_back(c);
    }

    m_mainWnd = std::make_unique<MainWnd>();
    if (!m_mainWnd->Create(hInst, gif, url))
    {
        throw std::runtime_error("MainWnd::Create failed");
    }

    ShowWindow(m_mainWnd->GetHwnd(), nCmdShow);
    UpdateWindow(m_mainWnd->GetHwnd());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
