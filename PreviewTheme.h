#pragma once
#include <Windows.h>
#include <memory>
#include <optional>

struct PreviewThemeSource {
    COLORREF background = GetSysColor(COLOR_WINDOW);
    COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
    bool highContrast = false;
    bool hasThemeColors = false;
};

struct PreviewThemeColors {
    COLORREF background;
    COLORREF text;
    COLORREF border;
    bool dark;
    bool highContrast;

    bool operator==(const PreviewThemeColors&) const = default;
};

inline PreviewThemeColors ResolvePreviewTheme(
    const PreviewThemeSource& source, std::optional<COLORREF> background, std::optional<COLORREF> text) noexcept {
    const bool systemColors = source.highContrast || source.hasThemeColors;
    const COLORREF effectiveBackground = systemColors ? source.background : background.value_or(source.background);
    const COLORREF effectiveText = systemColors ? source.text : text.value_or(source.text);
    const bool dark =
        2 * GetRValue(effectiveBackground) + 5 * GetGValue(effectiveBackground) + GetBValue(effectiveBackground) <
        8 * 128;
    return {effectiveBackground,
        effectiveText,
        source.highContrast ? effectiveText : (dark ? RGB(160, 160, 160) : RGB(80, 80, 80)),
        dark,
        source.highContrast};
}

// Unavailable or malformed personalization data does not confirm a theme.
inline std::optional<bool> DecodePreviewThemePreference(LSTATUS status, DWORD type, DWORD size, DWORD value) noexcept {
    if (status != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(DWORD) || value > 1) {
        return {};
    }
    return value == 0;
}

inline void ApplyPreviewThemePreference(PreviewThemeSource& source, std::optional<bool> dark) noexcept {
    if (!source.highContrast && !source.hasThemeColors && dark) {
        source.background = *dark ? RGB(30, 30, 30) : RGB(255, 255, 255);
        source.text = *dark ? RGB(240, 240, 240) : RGB(0, 0, 0);
        source.hasThemeColors = true;
    }
}

class PreviewThemeWatcher final {
public:
    PreviewThemeWatcher();
    ~PreviewThemeWatcher();
    void Start(HWND window, UINT message) noexcept;
    void Stop() noexcept;
    PreviewThemeSource Read() const noexcept;
    bool IsNotification(WPARAM cookie) const noexcept;
    bool HasSubscription() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

// Optional window-only integration, restricted to Windows 10 22H2 (build 19045).
void ApplyPreviewNativeTheme(HWND window, bool dark) noexcept;
