#include "GifAnimator.h"

#include <windows.h>
#include <gdiplus.h>
#include <d2d1helper.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
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

GifAnimator::GifAnimator() = default;
GifAnimator::~GifAnimator() = default;

namespace
{
    // GIF spec stores the delay in centiseconds (10 ms units). Many tools
    // encode incorrectly in milliseconds, so we accept either form.
    UINT NormalizeDelay(UINT raw)
    {
        if (raw == 0) return 100;
        return raw <= 100 ? raw * 10 : raw;
    }

    // GDI+ exposes the per-frame delays as one LONG array under
    // PropertyTagFrameDelay (0x5100), in centiseconds, indexed by frame number.
    // The array lives in the first page, so reading it before stepping through
    // the frames (the default active frame is 0) is enough. Returns `count`
    // delays, defaulting to 100 ms for any the file does not provide.
    std::vector<UINT> ReadFrameDelays(Gdiplus::Bitmap& bmp, UINT count)
    {
        std::vector<UINT> delays(count, 100);
        UINT size = bmp.GetPropertyItemSize(PropertyTagFrameDelay);
        if (size < sizeof(Gdiplus::PropertyItem)) return delays;

        std::vector<BYTE> storage(size);
        auto* item = reinterpret_cast<Gdiplus::PropertyItem*>(storage.data());
        if (bmp.GetPropertyItem(PropertyTagFrameDelay, size, item) != Gdiplus::Ok) return delays;
        if (!item->value || item->length < sizeof(LONG)) return delays;

        const LONG* raw = static_cast<const LONG*>(item->value);
        UINT available = item->length / sizeof(LONG);
        for (UINT i = 0; i < count && i < available; ++i)
            delays[i] = NormalizeDelay((UINT)raw[i]);
        return delays;
    }
}

bool GifAnimator::Load(ID2D1RenderTarget* rt, const wchar_t* path)
{
    if (!rt || !path) return false;

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &gsi, nullptr) != Gdiplus::Ok)
    {
        dlog(L"[GifAnimator] GdiplusStartup FAILED");
        return false;
    }

    bool ok = false;
    {
        // Constructing the Bitmap decodes the container; the frames themselves
        // are materialised lazily by SelectActiveFrame. Unlike
        // IWICBitmapFrameDecode this composites partial frames onto the full
        // logical screen, so every frame is a complete canvas.
        Gdiplus::Bitmap bmp(path, FALSE);
        if (bmp.GetLastStatus() != Gdiplus::Ok)
        {
            wchar_t b[160];
            std::swprintf(b, 160, L"[GifAnimator] Bitmap(%s) failed status=%d", path, (int)bmp.GetLastStatus());
            dlog(b);
        }
        else do
        {
            m_width = bmp.GetWidth();
            m_height = bmp.GetHeight();

            UINT count = bmp.GetFrameCount(&Gdiplus::FrameDimensionTime);
            if (count == 0)
            {
                dlog(L"[GifAnimator] GetFrameCount=0");
                break;
            }

            {
                wchar_t b[96];
                std::swprintf(b, 96, L"[GifAnimator] GetFrameCount=%u %ux%u", (unsigned)count, m_width, m_height);
                dlog(b);
            }

            std::vector<UINT> delays = ReadFrameDelays(bmp, count);

            m_frames.clear();
            m_frames.reserve(count);

            const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

            std::vector<BYTE> buf((size_t)m_width * 4 * m_height);

            for (UINT i = 0; i < count; ++i)
            {
                if (bmp.SelectActiveFrame(&Gdiplus::FrameDimensionTime, i) != Gdiplus::Ok)
                {
                    wchar_t b[128];
                    std::swprintf(b, 128, L"[GifAnimator] SelectActiveFrame(%u) failed", i);
                    dlog(b);
                    break;
                }

                Gdiplus::Rect rect(0, 0, (INT)m_width, (INT)m_height);
                Gdiplus::BitmapData bd = {};
                // PARGB is GDI+'s premultiplied BGRA, which is byte-for-byte
                // what D2D wants - no conversion pass needed.
                if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
                {
                    wchar_t b[128];
                    std::swprintf(b, 128, L"[GifAnimator] LockBits(%u) failed", i);
                    dlog(b);
                    break;
                }

                const BYTE* scan = static_cast<const BYTE*>(bd.Scan0);
                const size_t rowBytes = (size_t)m_width * 4;
                const ptrdiff_t stride = bd.Stride;
                for (UINT y = 0; y < m_height; ++y)
                {
                    // A negative stride means the rows are stored bottom-up.
                    const BYTE* src = stride >= 0
                        ? scan + (size_t)y * (size_t)stride
                        : scan + (size_t)(m_height - 1 - y) * (size_t)(-stride);
                    memcpy(buf.data() + (size_t)y * rowBytes, src, rowBytes);
                }
                bmp.UnlockBits(&bd);

                ComPtr<ID2D1Bitmap> d2dBitmap;
                HRESULT hr = rt->CreateBitmap(
                    D2D1::SizeU(m_width, m_height), buf.data(), (UINT32)rowBytes, props, &d2dBitmap);
                if (FAILED(hr))
                {
                    wchar_t b[144];
                    std::swprintf(b, 144, L"[GifAnimator] CreateBitmap(%u) %ux%u failed hr=0x%08lX",
                                  i, m_width, m_height, (unsigned long)hr);
                    dlog(b);
                    break;
                }

                Frame f;
                f.bitmap = d2dBitmap;
                f.delayMs = delays[i];
                m_frames.push_back(std::move(f));
            }

            m_index = 0;
            m_clock = 0;
            {
                wchar_t b[128];
                std::swprintf(b, 128, L"[GifAnimator] Load done: %u of %u frames decoded",
                              (unsigned)m_frames.size(), (unsigned)count);
                dlog(b);
            }
            ok = !m_frames.empty();
        } while (false);
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

UINT GifAnimator::CurrentDelayMs() const
{
    if (m_frames.empty()) return 100;
    return m_frames[m_index].delayMs;
}

size_t GifAnimator::Advance(DWORD elapsedMs)
{
    if (m_frames.empty()) return 0;
    if (m_paused) return m_index;
    m_clock += elapsedMs;
    size_t next = m_index;
    while (m_clock >= m_frames[next].delayMs)
    {
        m_clock -= m_frames[next].delayMs;
        next = (next + 1) % m_frames.size();
        if (next == m_index) { m_clock = 0; break; }
    }
    m_index = next;
    return m_index;
}

ID2D1Bitmap* GifAnimator::CurrentBitmap() const
{
    if (m_frames.empty()) return nullptr;
    return m_frames[m_index].bitmap.Get();
}
