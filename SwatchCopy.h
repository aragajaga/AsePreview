#pragma once

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

inline std::wstring FormatSwatchColor(const std::array<uint8_t, 3>& rgb, bool hex) {
    if (hex) {
        constexpr wchar_t digits[] = L"0123456789ABCDEF";
        std::wstring text = L"#";
        for (const auto component : rgb) {
            text += digits[component >> 4];
            text += digits[component & 15];
        }
        return text;
    }

    return std::to_wstring(rgb[0]) + L", " + std::to_wstring(rgb[1]) + L", " + std::to_wstring(rgb[2]);
}

inline HRESULT CopySwatchText(HWND owner, const std::wstring& text) {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return E_OUTOFMEMORY;
    }

    void* data = GlobalLock(memory);
    if (!data) {
        GlobalFree(memory);
        return E_OUTOFMEMORY;
    }
    std::memcpy(data, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return E_FAIL;
    }

    // The clipboard takes ownership only after the text has been accepted.
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) {
        GlobalFree(memory);
        return E_FAIL;
    }

    return S_OK;
}
