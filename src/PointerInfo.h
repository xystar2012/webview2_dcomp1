#pragma once

// ICoreWebView2PointerInfo is a COM interface with about 25 simple get_/put_
// properties. The WebView2 SDK does not ship a public factory for it, so
// the host has to provide its own implementation. We use WRL::RuntimeClass
// so QueryInterface / AddRef / Release are correct without hand-rolling
// reference counting.

#include <wrl.h>
#include <wrl/implements.h>
#include <WebView2.h>

#include <stdint.h>

struct PointerInfo final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    ICoreWebView2PointerInfo>
{
    PointerInfo() = default;

    // Convenience: copy fields from a Win32 POINTER_INFO into this object
    // so the WebView receives the same kind/position/timestamp it would in
// non-composition mode.
    void CopyFromWin32(const POINTER_INFO& pi);

#pragma region ICoreWebView2PointerInfo
    IFACEMETHOD(get_PointerKind)(_Out_ DWORD* value) override;
    IFACEMETHOD(put_PointerKind)(_In_ DWORD value) override;
    IFACEMETHOD(get_PointerId)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PointerId)(_In_ UINT32 value) override;
    IFACEMETHOD(get_FrameId)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_FrameId)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PointerFlags)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PointerFlags)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PointerDeviceRect)(_Out_ RECT* value) override;
    IFACEMETHOD(put_PointerDeviceRect)(_In_ RECT value) override;
    IFACEMETHOD(get_DisplayRect)(_Out_ RECT* value) override;
    IFACEMETHOD(put_DisplayRect)(_In_ RECT value) override;
    IFACEMETHOD(get_PixelLocation)(_Out_ POINT* value) override;
    IFACEMETHOD(put_PixelLocation)(_In_ POINT value) override;
    IFACEMETHOD(get_HimetricLocation)(_Out_ POINT* value) override;
    IFACEMETHOD(put_HimetricLocation)(_In_ POINT value) override;
    IFACEMETHOD(get_PixelLocationRaw)(_Out_ POINT* value) override;
    IFACEMETHOD(put_PixelLocationRaw)(_In_ POINT value) override;
    IFACEMETHOD(get_HimetricLocationRaw)(_Out_ POINT* value) override;
    IFACEMETHOD(put_HimetricLocationRaw)(_In_ POINT value) override;
    IFACEMETHOD(get_Time)(_Out_ DWORD* value) override;
    IFACEMETHOD(put_Time)(_In_ DWORD value) override;
    IFACEMETHOD(get_HistoryCount)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_HistoryCount)(_In_ UINT32 value) override;
    IFACEMETHOD(get_InputData)(_Out_ INT32* value) override;
    IFACEMETHOD(put_InputData)(_In_ INT32 value) override;
    IFACEMETHOD(get_KeyStates)(_Out_ DWORD* value) override;
    IFACEMETHOD(put_KeyStates)(_In_ DWORD value) override;
    IFACEMETHOD(get_PerformanceCount)(_Out_ UINT64* value) override;
    IFACEMETHOD(put_PerformanceCount)(_In_ UINT64 value) override;
    IFACEMETHOD(get_ButtonChangeKind)(_Out_ INT32* value) override;
    IFACEMETHOD(put_ButtonChangeKind)(_In_ INT32 value) override;
    IFACEMETHOD(get_PenFlags)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PenFlags)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PenMask)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PenMask)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PenPressure)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PenPressure)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PenRotation)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_PenRotation)(_In_ UINT32 value) override;
    IFACEMETHOD(get_PenTiltX)(_Out_ INT32* value) override;
    IFACEMETHOD(put_PenTiltX)(_In_ INT32 value) override;
    IFACEMETHOD(get_PenTiltY)(_Out_ INT32* value) override;
    IFACEMETHOD(put_PenTiltY)(_In_ INT32 value) override;
    IFACEMETHOD(get_TouchFlags)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_TouchFlags)(_In_ UINT32 value) override;
    IFACEMETHOD(get_TouchMask)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_TouchMask)(_In_ UINT32 value) override;
    IFACEMETHOD(get_TouchContact)(_Out_ RECT* value) override;
    IFACEMETHOD(put_TouchContact)(_In_ RECT value) override;
    IFACEMETHOD(get_TouchContactRaw)(_Out_ RECT* value) override;
    IFACEMETHOD(put_TouchContactRaw)(_In_ RECT value) override;
    IFACEMETHOD(get_TouchOrientation)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_TouchOrientation)(_In_ UINT32 value) override;
    IFACEMETHOD(get_TouchPressure)(_Out_ UINT32* value) override;
    IFACEMETHOD(put_TouchPressure)(_In_ UINT32 value) override;
#pragma endregion

private:
    DWORD   m_PointerKind = 0;
    UINT32  m_PointerId = 0;
    UINT32  m_FrameId = 0;
    UINT32  m_PointerFlags = 0;
    RECT    m_PointerDeviceRect{};
    RECT    m_DisplayRect{};
    POINT   m_PixelLocation{};
    POINT   m_HimetricLocation{};
    POINT   m_PixelLocationRaw{};
    POINT   m_HimetricLocationRaw{};
    DWORD   m_Time = 0;
    UINT32  m_HistoryCount = 0;
    INT32   m_InputData = 0;
    DWORD   m_KeyStates = 0;
    UINT64  m_PerformanceCount = 0;
    INT32   m_ButtonChangeKind = 0;
    UINT32  m_PenFlags = 0;
    UINT32  m_PenMask = 0;
    UINT32  m_PenPressure = 0;
    UINT32  m_PenRotation = 0;
    INT32   m_PenTiltX = 0;
    INT32   m_PenTiltY = 0;
    UINT32  m_TouchFlags = 0;
    UINT32  m_TouchMask = 0;
    RECT    m_TouchContact{};
    RECT    m_TouchContactRaw{};
    UINT32  m_TouchOrientation = 0;
    UINT32  m_TouchPressure = 0;
};
