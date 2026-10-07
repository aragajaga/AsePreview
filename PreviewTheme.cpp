#include "pch.h"
#include "PreviewTheme.h"
#include "Module.h"
#include <roapi.h>
#include <uxtheme.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <atomic>
#include <mutex>

namespace {
std::atomic<UINT_PTR> nextCookie{0};

struct NotificationTarget {
    NotificationTarget(HWND target, UINT notification) : window(target), message(notification), cookie(++nextCookie) {
        InterlockedIncrement(&g_objectCount);
    }
    ~NotificationTarget() {
        InterlockedDecrement(&g_objectCount);
    }

    std::mutex lock;
    HWND window;
    UINT message;
    const UINT_PTR cookie;
};
} // namespace

struct PreviewThemeWatcher::State {
    bool initialized = false;
    bool subscribed = false;
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    winrt::event_token token{};
    std::shared_ptr<NotificationTarget> target;

    ~State() {
        // Clear the destination before revocation; callbacks never capture the window object.
        if (target) {
            std::lock_guard guard(target->lock);
            target->window = nullptr;
        }
        if (subscribed) {
            try {
                settings.ColorValuesChanged(token);
            }
            catch (...) {
            }
        }
        settings = nullptr;
        target.reset();
        if (initialized) {
            RoUninitialize();
        }
    }
};

PreviewThemeWatcher::PreviewThemeWatcher() = default;
PreviewThemeWatcher::~PreviewThemeWatcher() = default;

void PreviewThemeWatcher::Start(HWND window, UINT message) noexcept {
    Stop();
    try {
        m_state = std::make_unique<State>();
        HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            hr = RoInitialize(RO_INIT_MULTITHREADED);
        }
        if (FAILED(hr)) {
            Stop();
            return;
        }
        m_state->initialized = true;
        m_state->settings = winrt::Windows::UI::ViewManagement::UISettings();
    }
    catch (...) {
        Stop();
        return;
    }

    // Theme colors remain usable even if the host cannot receive WinRT notifications.
    if (!IsWindow(window)) {
        return;
    }
    try {
        m_state->target = std::make_shared<NotificationTarget>(window, message);

        const auto target = m_state->target;
        m_state->token = m_state->settings.ColorValuesChanged([target](const auto&, const auto&) noexcept {
            std::lock_guard guard(target->lock);
            if (target->window) {
                PostMessageW(target->window, target->message, target->cookie, 0);
            }
        });
        m_state->subscribed = true;
    }
    catch (...) {
        if (m_state->target) {
            std::lock_guard guard(m_state->target->lock);
            m_state->target->window = nullptr;
        }
        m_state->target.reset();
    }
}

void PreviewThemeWatcher::Stop() noexcept {
    m_state.reset();
}

bool PreviewThemeWatcher::IsNotification(WPARAM cookie) const noexcept {
    return m_state && m_state->target && cookie == m_state->target->cookie;
}

bool PreviewThemeWatcher::HasSubscription() const noexcept {
    return m_state && m_state->subscribed;
}

PreviewThemeSource PreviewThemeWatcher::Read() const noexcept {
    PreviewThemeSource source;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    source.highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    if (!source.highContrast && m_state && m_state->settings) {
        try {
            const auto background =
                m_state->settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Background);
            const auto text =
                m_state->settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
            source.background = RGB(background.R, background.G, background.B);
            source.text = RGB(text.R, text.G, text.B);
            source.hasThemeColors = true;
        }
        catch (...) {
        }
    }
    if (!source.highContrast && !source.hasThemeColors) {
        DWORD value = 0, type = 0, size = sizeof(value);
        const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_ANY,
            &type,
            &value,
            &size);
        ApplyPreviewThemePreference(source, DecodePreviewThemePreference(status, type, size, value));
    }
    return source;
}

void ApplyPreviewNativeTheme(HWND window, bool dark) noexcept {
    using VersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    using AllowFunction = BOOL(WINAPI*)(HWND, BOOL);
    const auto version =
        reinterpret_cast<VersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW info{sizeof(info)};
    bool supported = false;
    if (version && version(&info) == 0 && info.dwMajorVersion == 10 && info.dwBuildNumber == 19045) {
        const HMODULE theme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (theme) {
            const auto allow = reinterpret_cast<AllowFunction>(GetProcAddress(theme, MAKEINTRESOURCEA(133)));
            if (allow) {
                supported = allow(window, dark);
            }
            FreeLibrary(theme);
        }
    }
    SetWindowTheme(window, dark && supported ? L"DarkMode_Explorer" : nullptr, nullptr);
}
