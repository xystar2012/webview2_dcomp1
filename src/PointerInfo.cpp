#include "PointerInfo.h"

// Macros collapse the repetitive get_/put_ bodies. Each field is plain-old
// data and the SDK semantics match Win32: get returns E_POINTER for null,
// put is a simple assignment.
#define IMPL_SCALAR(name, type)                                 \
    IFACEMETHODIMP PointerInfo::get_##name(type* value)        \
    {                                                           \
        if (!value) return E_POINTER;                           \
        *value = m_##name;                                      \
        return S_OK;                                            \
    }                                                           \
    IFACEMETHODIMP PointerInfo::put_##name(type value)         \
    {                                                           \
        m_##name = value;                                       \
        return S_OK;                                            \
    }

#define IMPL_RECT(name)                                         \
    IFACEMETHODIMP PointerInfo::get_##name(RECT* value)         \
    {                                                           \
        if (!value) return E_POINTER;                           \
        *value = m_##name;                                      \
        return S_OK;                                            \
    }                                                           \
    IFACEMETHODIMP PointerInfo::put_##name(RECT value)          \
    {                                                           \
        m_##name = value;                                       \
        return S_OK;                                            \
    }

#define IMPL_POINT(name)                                        \
    IFACEMETHODIMP PointerInfo::get_##name(POINT* value)        \
    {                                                           \
        if (!value) return E_POINTER;                           \
        *value = m_##name;                                      \
        return S_OK;                                            \
    }                                                           \
    IFACEMETHODIMP PointerInfo::put_##name(POINT value)         \
    {                                                           \
        m_##name = value;                                       \
        return S_OK;                                            \
    }

IMPL_SCALAR(PointerKind, DWORD)
IMPL_SCALAR(PointerId, UINT32)
IMPL_SCALAR(FrameId, UINT32)
IMPL_SCALAR(PointerFlags, UINT32)
IMPL_RECT(PointerDeviceRect)
IMPL_RECT(DisplayRect)
IMPL_POINT(PixelLocation)
IMPL_POINT(HimetricLocation)
IMPL_POINT(PixelLocationRaw)
IMPL_POINT(HimetricLocationRaw)
IMPL_SCALAR(Time, DWORD)
IMPL_SCALAR(HistoryCount, UINT32)
IMPL_SCALAR(InputData, INT32)
IMPL_SCALAR(KeyStates, DWORD)
IMPL_SCALAR(PerformanceCount, UINT64)
IMPL_SCALAR(ButtonChangeKind, INT32)
IMPL_SCALAR(PenFlags, UINT32)
IMPL_SCALAR(PenMask, UINT32)
IMPL_SCALAR(PenPressure, UINT32)
IMPL_SCALAR(PenRotation, UINT32)
IMPL_SCALAR(PenTiltX, INT32)
IMPL_SCALAR(PenTiltY, INT32)
IMPL_SCALAR(TouchFlags, UINT32)
IMPL_SCALAR(TouchMask, UINT32)
IMPL_RECT(TouchContact)
IMPL_RECT(TouchContactRaw)
IMPL_SCALAR(TouchOrientation, UINT32)
IMPL_SCALAR(TouchPressure, UINT32)

#undef IMPL_SCALAR
#undef IMPL_RECT
#undef IMPL_POINT

void PointerInfo::CopyFromWin32(const POINTER_INFO& pi)
{
    m_PointerKind = pi.pointerType;
    m_PointerId = pi.pointerId;
    m_FrameId = pi.frameId;
    m_PointerFlags = pi.pointerFlags;
    m_PixelLocation = pi.ptPixelLocation;
    m_PixelLocationRaw = pi.ptPixelLocationRaw;
    m_HimetricLocation = pi.ptHimetricLocation;
    m_HimetricLocationRaw = pi.ptHimetricLocationRaw;
    m_Time = pi.dwTime;
    m_HistoryCount = pi.historyCount;
    m_InputData = pi.InputData;
    m_KeyStates = pi.dwKeyStates;
    m_PerformanceCount = pi.PerformanceCount;
    m_ButtonChangeKind = static_cast<INT32>(pi.ButtonChangeType);
    // POINTER_INFO does not expose the touch contact rect (that lives on
    // POINTER_TOUCH_INFO). For non-touch pointers this stays at {0,0,0,0}.
    m_PointerDeviceRect = RECT{};
}
