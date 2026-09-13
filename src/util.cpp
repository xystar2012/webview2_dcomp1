#include "util.h"

#include <windows.h>
#include <shlobj.h>
#include <pathcch.h>
#include <combaseapi.h>

#include <sstream>
#include <stdexcept>

namespace util
{
    std::wstring exe_dir()
    {
        wchar_t path[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return {};
        std::wstring s(path, len);
        size_t pos = s.find_last_of(L'\\');
        if (pos == std::wstring::npos) return {};
        return s.substr(0, pos);
    }

    std::wstring join(const std::wstring& a, const std::wstring& b)
    {
        if (a.empty()) return b;
        if (a.back() == L'\\' || a.back() == L'/') return a + b;
        return a + L'\\' + b;
    }

    std::wstring abs_path(const std::wstring& rel)
    {
        std::wstring full = join(exe_dir(), rel);
        wchar_t out[MAX_PATH];
        if (GetFullPathNameW(full.c_str(), MAX_PATH, out, nullptr) == 0) return full;
        return out;
    }

    void set_dpi_awareness()
    {
        // Per-monitor v2: best for crisp UI; Windows 10 1703+.
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            using Fn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
            if (auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
            {
                if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
            }
        }
        // Fallback for older systems.
        SetProcessDPIAware();
    }

    RECT client_size(HWND hwnd)
    {
        RECT r{};
        if (hwnd) GetClientRect(hwnd, &r);
        return r;
    }

    double dpi_scale(HWND hwnd)
    {
        UINT dpi = 96;
        if (hwnd)
        {
            dpi = GetDpiForWindow(hwnd);
        }
        else
        {
            HDC dc = GetDC(nullptr);
            dpi = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(nullptr, dc);
        }
        return dpi / 96.0;
    }

    POINT screen_to_client(HWND hwnd, POINT p)
    {
        ScreenToClient(hwnd, &p);
        return p;
    }

    std::wstring hresult_message(HRESULT hr)
    {
        LPWSTR buf = nullptr;
        DWORD len = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(hr), 0,
            reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        std::wstring out = buf ? std::wstring(buf, len) : std::wstring{};
        if (buf) LocalFree(buf);
        if (out.empty())
        {
            std::wostringstream oss;
            oss << L"HRESULT 0x" << std::hex << static_cast<uint32_t>(hr);
            return oss.str();
        }
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' '))
            out.pop_back();
        return out;
    }

    ComInit::ComInit()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr)) throw std::runtime_error("CoInitializeEx failed");
    }

    ComInit::~ComInit()
    {
        CoUninitialize();
    }
}
