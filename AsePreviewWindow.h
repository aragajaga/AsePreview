#pragma once
#include <Windows.h>
#include "AsePalette.h"
#include "PreviewPaintBuffer.h"
#include "PreviewTheme.h"

class AsePreviewWindow final {
public:
    AsePreviewWindow() noexcept = default;
    ~AsePreviewWindow();

    AsePreviewWindow(const AsePreviewWindow&) = delete;
    AsePreviewWindow& operator=(const AsePreviewWindow&) = delete;

    HRESULT Create(HWND parent, const RECT& rect);
    HRESULT Place(HWND parent, const RECT& rect);

    HWND Handle() const {
        return m_window;
    }

    void SetContent(ase::Palette palette, const wchar_t* message);
    void SetColors(std::optional<COLORREF> background, std::optional<COLORREF> text);
    HRESULT SetFont(const LOGFONTW& font);

    bool HandleKey(MSG* message);

private:
    friend struct SwatchMenuTests;

    struct Tile {
        RECT rect;
        size_t entry;
        bool group;
    };

    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT Message(UINT, WPARAM, LPARAM);

    void RefreshTheme();
    void Layout();
    void Paint(HDC dc);
    void Scroll(int position);
    size_t HitTest(POINT point) const;
    std::optional<std::array<uint8_t, 3>> CopyColorAt(POINT point) const;
    void ShowContextMenu(POINT screenPoint);
    int Scale(int value) const;

    HWND m_window = nullptr;
    HWND m_tooltip = nullptr;

    HFONT m_font = nullptr;
    bool m_ownsFont = false;
    bool m_classLease = false;

    PreviewPaintBuffer m_buffer;
    PreviewThemeWatcher m_theme;
    std::optional<COLORREF> m_hostBackground;
    std::optional<COLORREF> m_hostText;
    bool m_applyingTheme = false;
    std::optional<PreviewThemeColors> m_appliedColors;
    COLORREF m_border = RGB(80, 80, 80);

    COLORREF m_background = GetSysColor(COLOR_WINDOW);
    COLORREF m_text = GetSysColor(COLOR_WINDOWTEXT);

    ase::Palette m_palette;
    const wchar_t* m_message = nullptr;
    std::vector<Tile> m_tiles;
    std::wstring m_tip;

    int m_scroll = 0;
    int m_contentHeight = 0;
    int m_wheelRemainder = 0;
    UINT m_dpi = 96;
    size_t m_hot = size_t(-1);
};
