#include "pch.h"
#include "AsePreviewWindow.h"
#include "Module.h"
#include "SwatchCopy.h"
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <algorithm>
#include <new>

namespace {
constexpr wchar_t ClassName[] = L"Aragajaga.AsePreviewWindow";
SRWLOCK classLock = SRWLOCK_INIT;
unsigned classUsers = 0;
constexpr UINT ThemeNotification = WM_APP + 1;
constexpr UINT_PTR ThemeTimer = 1;

HRESULT WinError() {
    const DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

} // namespace

AsePreviewWindow::~AsePreviewWindow() {
    if (m_window) {
        DestroyWindow(m_window);
    }

    if (m_ownsFont) {
        DeleteObject(m_font);
    }

    if (m_classLease) {
        AcquireSRWLockExclusive(&classLock);
        if (--classUsers == 0) {
            UnregisterClassW(ClassName, g_hInstance);
        }
        ReleaseSRWLockExclusive(&classLock);
    }
}

HRESULT AsePreviewWindow::Create(HWND parent, const RECT& rect) {
    if (m_window) {
        return Place(parent, rect);
    }

    // Keep the shared window class registered while any preview owns a lease.
    if (!m_classLease) {
        AcquireSRWLockExclusive(&classLock);
        HRESULT hr = S_OK;

        if (!classUsers) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = ClassName;
            if (!RegisterClassW(&wc)) {
                hr = WinError();
            }
        }

        if (SUCCEEDED(hr)) {
            ++classUsers;
            m_classLease = true;
        }
        ReleaseSRWLockExclusive(&classLock);

        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!m_font) {
        m_font = HFONT(GetStockObject(DEFAULT_GUI_FONT));
    }

    if (!CreateWindowExW(0,
            ClassName,
            L"ASE Palette",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
            rect.left,
            rect.top,
            std::max(0L, rect.right - rect.left),
            std::max(0L, rect.bottom - rect.top),
            parent,
            nullptr,
            g_hInstance,
            this)) {
        return WinError();
    }

    return S_OK;
}

HRESULT AsePreviewWindow::Place(HWND parent, const RECT& rect) {
    if (!m_window) {
        return E_UNEXPECTED;
    }

    if (GetParent(m_window) != parent) {
        SetLastError(0);
        if (!SetParent(m_window, parent) && GetLastError()) {
            return WinError();
        }
    }

    if (!MoveWindow(m_window,
            rect.left,
            rect.top,
            std::max(0L, rect.right - rect.left),
            std::max(0L, rect.bottom - rect.top),
            TRUE)) {
        return WinError();
    }

    return S_OK;
}

void AsePreviewWindow::SetContent(ase::Palette palette, const wchar_t* message) {
    m_palette = std::move(palette);
    m_message = message;
    m_scroll = 0;
    m_hot = size_t(-1);

    if (m_tooltip) {
        SendMessageW(m_tooltip, TTM_POP, 0, 0);
    }

    Layout();
    InvalidateRect(m_window, nullptr, FALSE);
}

void AsePreviewWindow::SetColors(std::optional<COLORREF> background, std::optional<COLORREF> text) {
    m_hostBackground = background;
    m_hostText = text;
    if (m_window) {
        RefreshTheme();
    }
}

void AsePreviewWindow::RefreshTheme() {
    if (m_applyingTheme) {
        return;
    }
    const auto colors = ResolvePreviewTheme(m_theme.Read(), m_hostBackground, m_hostText);
    if (m_appliedColors && *m_appliedColors == colors) {
        return;
    }
    m_applyingTheme = true;
    m_appliedColors = colors;
    m_background = colors.background;
    m_text = colors.text;
    m_border = colors.border;

    ApplyPreviewNativeTheme(m_window, colors.dark && !colors.highContrast);
    if (m_tooltip) {
        // Native tooltip colors only take effect when its visual style is disabled.
        SetWindowTheme(m_tooltip, colors.highContrast ? nullptr : L"", colors.highContrast ? nullptr : L"");
        if (!colors.highContrast) {
            SendMessageW(m_tooltip, TTM_SETTIPBKCOLOR, m_background, 0);
            SendMessageW(m_tooltip, TTM_SETTIPTEXTCOLOR, m_text, 0);
        }
    }
    m_applyingTheme = false;
    InvalidateRect(m_window, nullptr, FALSE);
}

HRESULT AsePreviewWindow::SetFont(const LOGFONTW& font) {
    const HFONT replacement = CreateFontIndirectW(&font);
    if (!replacement) {
        return WinError();
    }

    if (m_ownsFont) {
        DeleteObject(m_font);
    }

    m_font = replacement;
    m_ownsFont = true;

    if (m_window) {
        Layout();
        InvalidateRect(m_window, nullptr, FALSE);
    }

    return S_OK;
}

int AsePreviewWindow::Scale(int value) const {
    return MulDiv(value, int(m_dpi), 96);
}

void AsePreviewWindow::Layout() {
    if (!m_window) {
        return;
    }

    m_dpi = GetDpiForWindow(m_window);
    if (!m_dpi) {
        m_dpi = 96;
    }

    RECT client{};
    GetClientRect(m_window, &client);
    const int margin = Scale(8), gap = Scale(8);

    // Use the selected font to reserve enough space for labels and group headings.
    int textHeight = Scale(18);
    HDC dc = GetDC(m_window);
    if (dc) {
        const auto oldFont = SelectObject(dc, m_font);
        TEXTMETRICW metrics{};
        if (GetTextMetricsW(dc, &metrics)) {
            textHeight = metrics.tmHeight + Scale(4);
        }

        SelectObject(dc, oldFont);
        ReleaseDC(m_window, dc);
    }

    // A persistent scrollbar avoids oscillation as its width changes the column count.
    const int available = std::max(1, int(client.right) - 2 * margin);
    const int columns = std::max(1, (available + gap) / (Scale(112) + gap));
    const int tileWidth = std::max(1, (available - (columns - 1) * gap) / columns);
    const int tileHeight = Scale(56) + textHeight;

    int y = margin, column = 0;
    unsigned depth = 0;
    m_tiles.clear();
    m_tiles.reserve(m_palette.entries.size());

    // Group headings span a row; color entries fill the calculated grid.
    for (size_t i = 0; i < m_palette.entries.size(); ++i) {
        const auto& entry = m_palette.entries[i];

        if (entry.kind != ase::EntryKind::Color) {
            if (column) {
                y += tileHeight + gap;
                column = 0;
            }

            if (entry.kind == ase::EntryKind::GroupStart) {
                const int indent = Scale(int(std::min(depth, 8u)) * 8);
                m_tiles.push_back({{margin + indent, y, client.right - margin, y + textHeight}, i, true});
                y += textHeight + gap;
                ++depth;
            }
            else if (depth) {
                --depth;
            }
        }
        else {
            const int x = margin + column * (tileWidth + gap);
            m_tiles.push_back({{x, y, x + tileWidth, y + tileHeight}, i, false});
            if (++column == columns) {
                y += tileHeight + gap;
                column = 0;
            }
        }
    }

    m_contentHeight = y + (column ? tileHeight + gap : 0) + margin;
    Scroll(m_scroll);

    if (m_tooltip) {
        TOOLINFOW tool{sizeof(tool)};
        tool.hwnd = m_window;
        tool.uId = 1;
        tool.rect = client;
        SendMessageW(m_tooltip, TTM_NEWTOOLRECTW, 0, LPARAM(&tool));
    }
}

void AsePreviewWindow::Scroll(int position) {
    RECT client{};
    GetClientRect(m_window, &client);
    m_scroll = std::clamp(position, 0, std::max(0, m_contentHeight - int(client.bottom)));

    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    info.nMax = std::max(0, m_contentHeight - 1);
    info.nPage = UINT(std::max(0L, client.bottom));
    info.nPos = m_scroll;
    SetScrollInfo(m_window, SB_VERT, &info, TRUE);

    if (m_tooltip) {
        SendMessageW(m_tooltip, TTM_POP, 0, 0);
    }
    m_hot = size_t(-1);
    InvalidateRect(m_window, nullptr, FALSE);
}

size_t AsePreviewWindow::HitTest(POINT point) const {
    point.y += m_scroll;

    for (const auto& tile : m_tiles) {
        if (!tile.group && PtInRect(&tile.rect, point)) {
            return tile.entry;
        }
    }

    return size_t(-1);
}

std::optional<std::array<uint8_t, 3>> AsePreviewWindow::CopyColorAt(POINT point) const {
    const size_t index = HitTest(point);
    if (index == size_t(-1) || index >= m_palette.entries.size()) {
        return std::nullopt;
    }

    const auto& entry = m_palette.entries[index];
    if (entry.kind != ase::EntryKind::Color || !entry.supported) {
        return std::nullopt;
    }

    return entry.rgb;
}

void AsePreviewWindow::ShowContextMenu(POINT screenPoint) {
    if (screenPoint.x == -1 && screenPoint.y == -1) {
        return;
    }

    POINT clientPoint = screenPoint;
    ScreenToClient(m_window, &clientPoint);
    const auto rgb = CopyColorAt(clientPoint);
    if (!rgb) {
        return;
    }

    const HWND owner = m_window;
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        MessageBoxW(owner, L"Unable to open the color menu.", L"ASE Palette", MB_OK | MB_ICONERROR);
        return;
    }
    if (!AppendMenuW(menu, MF_STRING, 1, L"Copy as HEX") || !AppendMenuW(menu, MF_STRING, 2, L"Copy as RGB")) {
        DestroyMenu(menu);
        MessageBoxW(owner, L"Unable to open the color menu.", L"ASE Palette", MB_OK | MB_ICONERROR);
        return;
    }

    if (m_tooltip) {
        SendMessageW(m_tooltip, TTM_POP, 0, 0);
    }
    const UINT command = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, owner, nullptr);
    DestroyMenu(menu);

    // Menu tracking can dispatch messages that replace the palette or destroy the window.
    if ((command == 1 || command == 2) && IsWindow(owner)) {
        if (FAILED(CopySwatchText(owner, FormatSwatchColor(*rgb, command == 1)))) {
            MessageBoxW(owner, L"Unable to copy the color to the clipboard.", L"ASE Palette", MB_OK | MB_ICONERROR);
        }
    }
}

void AsePreviewWindow::Paint(HDC dc) {
    const int saved = SaveDC(dc);
    RECT client{};
    GetClientRect(m_window, &client);
    SetDCBrushColor(dc, m_background);
    FillRect(dc, &client, HBRUSH(GetStockObject(DC_BRUSH)));

    const auto oldFont = SelectObject(dc, m_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, m_text);

    if (m_message || !m_palette.colorCount) {
        RECT text = client;
        InflateRect(&text, -Scale(12), -Scale(12));
        DrawTextW(dc,
            m_message ? m_message : L"The palette contains no colors.",
            -1,
            &text,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
    else {
        for (const auto& tile : m_tiles) {
            RECT rect = tile.rect;
            OffsetRect(&rect, 0, -m_scroll);
            if (rect.bottom <= 0 || rect.top >= client.bottom) {
                continue;
            }

            const auto& entry = m_palette.entries[tile.entry];
            if (!tile.group) {
                RECT swatch = rect;
                swatch.bottom = rect.top + Scale(52);
                if (entry.supported) {
                    SetDCBrushColor(dc, RGB(entry.rgb[0], entry.rgb[1], entry.rgb[2]));
                    FillRect(dc, &swatch, HBRUSH(GetStockObject(DC_BRUSH)));
                }
                else {
                    DrawTextW(dc, L"?", 1, &swatch, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                SetDCBrushColor(dc, m_border);
                FrameRect(dc, &swatch, HBRUSH(GetStockObject(DC_BRUSH)));
                rect.top = swatch.bottom + Scale(4);
            }

            const wchar_t* name =
                entry.name.empty() ? (tile.group ? L"Unnamed group" : L"Unnamed") : entry.name.c_str();
            DrawTextW(dc, name, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    SelectObject(dc, oldFont);
    if (saved) {
        RestoreDC(dc, saved);
    }
}

bool AsePreviewWindow::HandleKey(MSG* message) {
    if (!message || !m_window || message->message != WM_KEYDOWN ||
        (GetFocus() != m_window && !IsChild(m_window, GetFocus()))) {
        return false;
    }
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) {
        return false;
    }

    RECT client{};
    GetClientRect(m_window, &client);

    switch (message->wParam) {
    case VK_UP:
        Scroll(m_scroll - Scale(32));
        return true;
    case VK_DOWN:
        Scroll(m_scroll + Scale(32));
        return true;
    case VK_PRIOR:
        Scroll(m_scroll - int(client.bottom));
        return true;
    case VK_NEXT:
        Scroll(m_scroll + int(client.bottom));
        return true;
    case VK_HOME:
        Scroll(0);
        return true;
    case VK_END:
        Scroll(m_contentHeight);
        return true;
    }

    return false;
}

LRESULT CALLBACK AsePreviewWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    auto* self = reinterpret_cast<AsePreviewWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<AsePreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(self));
    }

    if (!self) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    try {
        return self->Message(message, wParam, lParam);
    }
    catch (...) {
        self->m_message = L"Unable to display the palette: insufficient resources.";
        self->m_tiles.clear();

        if (message == WM_NCCREATE) {
            return FALSE;
        }

        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
}

LRESULT AsePreviewWindow::Message(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);

        m_tooltip = CreateWindowExW(WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            m_window,
            nullptr,
            g_hInstance,
            nullptr);
        if (m_tooltip) {
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_SUBCLASS;
            tool.hwnd = m_window;
            tool.uId = 1;
            tool.lpszText = LPSTR_TEXTCALLBACKW;
            GetClientRect(m_window, &tool.rect);
            SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, LPARAM(&tool));
            SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, Scale(480));
        }

        m_theme.Start(m_window, ThemeNotification);
        if (!m_theme.HasSubscription()) {
            SetTimer(m_window, ThemeTimer, 1000, nullptr);
        }
        RefreshTheme();
        Layout();
        return 0;
    }
    case WM_TIMER:
        if (wParam == ThemeTimer) {
            RefreshTheme();
            return 0;
        }
        break;
    case ThemeNotification:
        if (m_theme.IsNotification(wParam)) {
            RefreshTheme();
        }
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        RefreshTheme();
        break;
    case WM_SIZE:
    case WM_DPICHANGED_AFTERPARENT:
        Layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        Paint(reinterpret_cast<HDC>(wParam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(m_window, &paint);

        RECT client{};
        GetClientRect(m_window, &client);
        if (dc && m_buffer.Ensure(dc, client.right, client.bottom)) {
            Paint(m_buffer.DC());
            if (!BitBlt(dc, 0, 0, client.right, client.bottom, m_buffer.DC(), 0, 0, SRCCOPY)) {
                Paint(dc);
            }
        }
        else if (dc) {
            Paint(dc);
        }

        EndPaint(m_window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
        ::SetFocus(m_window);
        return 0;
    case WM_CONTEXTMENU:
        ShowContextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_MOUSEWHEEL: {
        m_wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = m_wheelRemainder / WHEEL_DELTA;
        m_wheelRemainder %= WHEEL_DELTA;

        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        RECT client{};
        GetClientRect(m_window, &client);
        const int distance = lines == WHEEL_PAGESCROLL ? int(client.bottom) : Scale(16) * int(std::min(lines, 100u));

        Scroll(m_scroll - steps * distance);
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
        GetScrollInfo(m_window, SB_VERT, &info);
        RECT client{};
        GetClientRect(m_window, &client);

        int position = m_scroll;
        switch (LOWORD(wParam)) {
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = m_contentHeight;
            break;
        case SB_LINEUP:
            position -= Scale(32);
            break;
        case SB_LINEDOWN:
            position += Scale(32);
            break;
        case SB_PAGEUP:
            position -= client.bottom;
            break;
        case SB_PAGEDOWN:
            position += client.bottom;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = info.nTrackPos;
            break;
        }

        Scroll(position);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const auto hot = HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (hot != m_hot) {
            m_hot = hot;
            if (m_tooltip) {
                SendMessageW(m_tooltip, TTM_POP, 0, 0);

                TOOLINFOW tool{sizeof(tool)};
                tool.hwnd = m_window;
                tool.uId = 1;
                tool.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(m_tooltip, TTM_UPDATETIPTEXTW, 0, LPARAM(&tool));
            }
        }

        return 0;
    }
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification && notification->hwndFrom == m_tooltip && notification->code == TTN_GETDISPINFOW) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(m_window, &point);

            const auto index = HitTest(point);
            m_tip = index == size_t(-1) ? L"" : ase::Describe(m_palette.entries[index]);
            reinterpret_cast<NMTTDISPINFOW*>(lParam)->lpszText = m_tip.data();
            return 0;
        }

        break;
    }
    case WM_DESTROY:
        KillTimer(m_window, ThemeTimer);
        m_theme.Stop();
        m_buffer.Reset();
        // A popup tooltip can be owned by the top-level host rather than the child preview.
        if (m_tooltip) {
            DestroyWindow(m_tooltip);
            m_tooltip = nullptr;
        }
        return 0;
    case WM_NCDESTROY: {
        KillTimer(m_window, ThemeTimer);
        m_theme.Stop();
        m_buffer.Reset();
        m_appliedColors.reset();

        const HWND window = m_window;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        m_window = nullptr;
        m_tooltip = nullptr;

        return DefWindowProcW(window, message, wParam, lParam);
    }
    }

    return DefWindowProcW(m_window, message, wParam, lParam);
}
