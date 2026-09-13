#pragma once

// WIN32_LEAN_AND_MEAN / NOMINMAX / UNICODE are defined as target_compile_
// definitions in CMakeLists.txt so we don't fight other headers here.
// We *do* want to make sure <windows.h> is available, since the rest of the
// project pulls in WRL / d2d1 / dcomp / wincodec etc. which need it.
#include <windows.h>

#include <wrl/client.h>
#include <wrl/implements.h>

#include <string>

namespace util
{
    // Path utilities.
    std::wstring exe_dir();
    std::wstring join(const std::wstring& a, const std::wstring& b);
    std::wstring abs_path(const std::wstring& rel);

    // Window helpers.
    void set_dpi_awareness();
    RECT client_size(HWND hwnd);
    double dpi_scale(HWND hwnd);
    POINT screen_to_client(HWND hwnd, POINT p);

    // Diagnostics.
    std::wstring hresult_message(HRESULT hr);

    // RAII wrappers.
    struct ComInit
    {
        ComInit();
        ~ComInit();
    };
}
