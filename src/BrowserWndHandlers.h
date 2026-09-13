#pragma once

// Definitions of the WebView2 event handlers used by BrowserWnd. They must
// live in a header so ComPtr<T> can find T::Release()/AddRef() when it
// instantiates its destructor in BrowserWnd.h.
//
// The handlers do not hold a BrowserWnd pointer; instead they invoke a
// std::function supplied at construction time. This avoids the cyclic
// include between BrowserWnd.h and the handler definitions.

#include <windows.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <WebView2.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>

namespace bwh
{
    inline void dlog(const wchar_t* msg)
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

class AccelHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2AcceleratorKeyPressedEventHandler>
{
public:
    explicit AccelHandler(std::function<void(UINT vk)> cb) : m_cb(std::move(cb)) {}
    IFACEMETHODIMP Invoke(
        ICoreWebView2Controller*,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override
    {
        COREWEBVIEW2_KEY_EVENT_KIND kind{};
        UINT vk = 0;
        if (SUCCEEDED(args->get_KeyEventKind(&kind)) &&
            SUCCEEDED(args->get_VirtualKey(&vk)) && m_cb)
        {
            m_cb(vk);
        }
        if (args) args->put_Handled(TRUE);
        return S_OK;
    }
private:
    std::function<void(UINT vk)> m_cb;
};

class RasterizationScaleHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2RasterizationScaleChangedEventHandler>
{
public:
    explicit RasterizationScaleHandler(std::function<void()> cb) : m_cb(std::move(cb)) {}
    IFACEMETHODIMP Invoke(ICoreWebView2Controller*, IUnknown*) override
    {
        if (m_cb) m_cb();
        return S_OK;
    }
private:
    std::function<void()> m_cb;
};

class EnvCompletedHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>
{
public:
    using Cb = std::function<void(HRESULT, ICoreWebView2Environment*)>;
    explicit EnvCompletedHandler(Cb cb) : m_cb(std::move(cb)) {}
    IFACEMETHODIMP Invoke(HRESULT hr, ICoreWebView2Environment* env) override
    {
        if (m_cb) m_cb(hr, env);
        return S_OK;
    }
private:
    Cb m_cb;
};

class NavCompletedHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2NavigationCompletedEventHandler>
{
public:
    using Cb = std::function<void(HRESULT /*webErrorStatus*/,
                                 const wchar_t* /*source*/)>;
    explicit NavCompletedHandler(Cb cb) : m_cb(std::move(cb)) {}
    IFACEMETHODIMP Invoke(
        ICoreWebView2*,
        ICoreWebView2NavigationCompletedEventArgs* args) override
    {
        if (!m_cb) return S_OK;
        HRESULT status = S_OK;
        if (args) args->get_WebErrorStatus(reinterpret_cast<COREWEBVIEW2_WEB_ERROR_STATUS*>(&status));
        m_cb(status, nullptr);
        return S_OK;
    }
private:
    Cb m_cb;
};

class ControllerCompletedHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>
{
public:
    using Cb = std::function<void(HRESULT, ICoreWebView2CompositionController*)>;
    explicit ControllerCompletedHandler(Cb cb) : m_cb(std::move(cb)) {}
    IFACEMETHODIMP Invoke(HRESULT hr, ICoreWebView2CompositionController* ctrl) override
    {
        if (m_cb) m_cb(hr, ctrl);
        return S_OK;
    }
private:
    Cb m_cb;
};