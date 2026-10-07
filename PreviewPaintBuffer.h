#pragma once
#include <Windows.h>

// Own the selected bitmap together with its DC; never delete a selected GDI object.
class PreviewPaintBuffer final {
public:
    ~PreviewPaintBuffer() {
        Reset();
    }
    PreviewPaintBuffer() = default;
    PreviewPaintBuffer(const PreviewPaintBuffer&) = delete;
    PreviewPaintBuffer& operator=(const PreviewPaintBuffer&) = delete;

    void Reset() noexcept {
        if (m_dc) {
            SelectObject(m_dc, m_original);
            DeleteObject(m_bitmap);
            DeleteDC(m_dc);
        }
        m_dc = nullptr;
        m_bitmap = nullptr;
        m_original = nullptr;
        m_width = m_height = 0;
    }

    bool Ensure(HDC reference, int width, int height) noexcept {
        if (!reference || width <= 0 || height <= 0) {
            Reset();
            return false;
        }
        if (m_dc && width == m_width && height == m_height) {
            return true;
        }

        Reset();
        const HDC dc = CreateCompatibleDC(reference);
        const HBITMAP bitmap = dc ? CreateCompatibleBitmap(reference, width, height) : nullptr;
        const HGDIOBJ original = bitmap ? SelectObject(dc, bitmap) : nullptr;
        if (!original || original == HGDI_ERROR) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            if (dc) {
                DeleteDC(dc);
            }
            return false;
        }

        m_dc = dc;
        m_bitmap = bitmap;
        m_original = original;
        m_width = width;
        m_height = height;
        return true;
    }

    HDC DC() const noexcept {
        return m_dc;
    }

private:
    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_original = nullptr;
    int m_width = 0;
    int m_height = 0;
};
