#pragma once

#include <wrl/client.h>
#include <d2d1.h>

#include <vector>

// Decodes an animated GIF into a list of D2D bitmaps, one per frame, with
// per-frame delays. Uses GDI+ for decoding and Direct2D for the in-memory
// bitmaps so VideoWnd can draw them at native speed.
//
// GDI+ rather than WIC on purpose: WIC's GIF decoder returns garbage pixels
// for runs of frames in some files (a 17-frame black-and-white snow run in
// marketplace.gif, reproducible through every conversion path WIC offers).
// GDI+ decodes the same frames correctly and also composites partial frames
// onto the canvas, which IWICBitmapFrameDecode does not do at all.
class GifAnimator
{
public:
    GifAnimator();
    ~GifAnimator();

    // Opens the GIF file and pre-decodes every frame. Returns false on
    // failure (file missing, not a GIF, decoder error, ...).
    bool Load(ID2D1RenderTarget* rt, const wchar_t* path);

    size_t FrameCount() const { return m_frames.size(); }
    bool IsLoaded() const { return !m_frames.empty(); }
    UINT  CurrentDelayMs() const;

    // Advance the playback clock by elapsed milliseconds and return the new
    // frame index. Wraps around when the total duration has been consumed.
    // A no-op while paused, so the caller keeps drawing the same frame.
    size_t Advance(DWORD elapsedMs);

    // Playback can be frozen on the current frame and resumed later. The
    // elapsed time that passes while paused is discarded, not banked, so
    // resuming continues from the frame that was on screen.
    void SetPaused(bool paused) { m_paused = paused; }
    void TogglePaused() { m_paused = !m_paused; }
    bool IsPaused() const { return m_paused; }

    // The currently visible frame. Pointer is valid for the lifetime of the
    // animator. Returns nullptr before Load() / after a failed Load().
    ID2D1Bitmap* CurrentBitmap() const;
    size_t       CurrentIndex() const { return m_index; }

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

private:
    struct Frame
    {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        UINT delayMs = 100;
    };

    std::vector<Frame> m_frames;
    size_t m_index = 0;
    DWORD m_clock = 0;
    bool  m_paused = false;
    UINT m_width = 0;
    UINT m_height = 0;
};
