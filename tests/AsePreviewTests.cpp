#include "framework.h"
#include "AsePalette.h"
#include "AsePreviewWindow.h"
#include "SwatchCopy.h"
#include "AseStream.h"
#include "Guids.h"
#include "PreviewTheme.h"
#include "PreviewPaintBuffer.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <oleidl.h>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>

LONG g_objectCount = 0;
HINSTANCE g_hInstance = GetModuleHandleW(nullptr);

struct SwatchMenuTests {
    static bool Run() {
        AsePreviewWindow preview;
        ase::Entry color;
        color.supported = true;
        color.rgb = {255, 128, 0};
        ase::Entry group;
        group.kind = ase::EntryKind::GroupStart;
        ase::Entry unknown;
        preview.m_palette.entries = {color, group, unknown};
        preview.m_tiles = {
            {{0, 100, 100, 180}, 0, false}, {{0, 180, 100, 210}, 1, true}, {{0, 210, 100, 290}, 2, false}};
        preview.m_scroll = 100;

        const auto swatch = preview.CopyColorAt({10, 10});
        const auto name = preview.CopyColorAt({10, 75});
        return swatch == color.rgb && name == color.rgb && !preview.CopyColorAt({10, 90}) &&
            !preview.CopyColorAt({10, 120}) && !preview.CopyColorAt({150, 10}) && !preview.CopyColorAt({10, -1});
    }
};

namespace {
unsigned checks = 0;

void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(description);
    }
}

void SwatchCopyTests() {
    Check(FormatSwatchColor({0, 0, 0}, true) == L"#000000", "black HEX");
    Check(FormatSwatchColor({255, 255, 255}, true) == L"#FFFFFF", "white HEX");
    Check(FormatSwatchColor({255, 128, 0}, true) == L"#FF8000", "mixed HEX");
    Check(FormatSwatchColor({1, 10, 15}, true) == L"#010A0F", "HEX leading zeros");
    Check(FormatSwatchColor({0, 0, 0}, false) == L"0, 0, 0", "black RGB");
    Check(FormatSwatchColor({255, 255, 255}, false) == L"255, 255, 255", "white RGB");
    Check(FormatSwatchColor({255, 128, 0}, false) == L"255, 128, 0", "integer RGB");
    Check(SwatchMenuTests::Run(), "copy hit testing includes names and scroll, excludes non-colors");
}

void SwatchClipboardTests() {
    // This opt-in test replaces the clipboard; ordinary test runs never call it.
    const HWND owner = CreateWindowExW(
        0, L"STATIC", L"Clipboard test", 0, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(owner != nullptr, "clipboard owner");

    for (const bool hex : {true, false}) {
        const auto expected = FormatSwatchColor({255, 128, 0}, hex);
        const HRESULT copied = CopySwatchText(owner, expected);
        if (FAILED(copied)) {
            DestroyWindow(owner);
            Check(false, "write clipboard");
        }

        const bool opened = OpenClipboard(owner) != FALSE;
        bool matches = false;
        if (opened) {
            const HANDLE memory = GetClipboardData(CF_UNICODETEXT);
            const auto* text = memory ? static_cast<const wchar_t*>(GlobalLock(memory)) : nullptr;
            matches = text && expected == text && GlobalSize(memory) >= (expected.size() + 1) * sizeof(wchar_t);
            if (text) {
                GlobalUnlock(memory);
            }
            CloseClipboard();
        }
        if (!matches) {
            DestroyWindow(owner);
        }
        Check(matches, "read Unicode clipboard text including terminator");
    }

    DestroyWindow(owner);
}

void ThemeAndBufferTests() {
    const PreviewThemeSource light{RGB(255, 255, 255), RGB(0, 0, 0), false, true};
    const PreviewThemeSource dark{RGB(0, 0, 0), RGB(255, 255, 255), false, true};
    const auto lightColors = ResolvePreviewTheme(light, {}, {});
    const auto darkColors = ResolvePreviewTheme(dark, {}, {});
    Check(!lightColors.dark && darkColors.dark, "system light and dark selection");
    Check(lightColors.border != lightColors.background && darkColors.border != darkColors.background,
        "borders contrast with background");
    Check(ResolvePreviewTheme(dark, RGB(255, 255, 255), RGB(0, 0, 0)) == darkColors,
        "Shell light host colors cannot override Windows dark theme");

    for (const auto& source : {light, dark}) {
        for (const auto background : {std::optional<COLORREF>{}, std::optional<COLORREF>{RGB(30, 30, 30)}}) {
            for (const auto text : {std::optional<COLORREF>{}, std::optional<COLORREF>{RGB(240, 240, 240)}}) {
                Check(ResolvePreviewTheme(source, background, text) == ResolvePreviewTheme(source, {}, {}),
                    "Windows theme takes priority over partial or complete host colors");
                auto contrast = source;
                contrast.highContrast = true;
                const auto accessible = ResolvePreviewTheme(contrast, background, text);
                Check(accessible.highContrast && accessible.background == source.background &&
                        accessible.text == source.text && accessible.border == source.text,
                    "high contrast takes priority over host and theme colors");
            }
        }

        auto unavailable = source;
        unavailable.hasThemeColors = false;
        const auto backgroundOnly = ResolvePreviewTheme(unavailable, RGB(30, 30, 30), {});
        const auto textOnly = ResolvePreviewTheme(unavailable, {}, RGB(240, 240, 240));
        Check(backgroundOnly.background == RGB(30, 30, 30) && backgroundOnly.text == source.text,
            "independent fallback host background");
        Check(textOnly.background == source.background && textOnly.text == RGB(240, 240, 240),
            "independent fallback host text");
    }

    const PreviewThemeSource fallback;
    const auto fallbackColors = ResolvePreviewTheme(fallback, {}, {});
    Check(
        fallbackColors.background == GetSysColor(COLOR_WINDOW) && fallbackColors.text == GetSysColor(COLOR_WINDOWTEXT),
        "no theme or host colors falls back to Win32");
    Check(DecodePreviewThemePreference(ERROR_SUCCESS, REG_DWORD, sizeof(DWORD), 0) == true,
        "registry DWORD zero means dark");
    Check(DecodePreviewThemePreference(ERROR_SUCCESS, REG_DWORD, sizeof(DWORD), 1) == false,
        "registry DWORD one means light");
    Check(!DecodePreviewThemePreference(ERROR_SUCCESS, REG_DWORD, sizeof(DWORD), 2), "reject unknown registry value");
    Check(!DecodePreviewThemePreference(ERROR_SUCCESS, REG_SZ, sizeof(DWORD), 0), "reject wrong registry type");
    Check(!DecodePreviewThemePreference(ERROR_SUCCESS, REG_DWORD, 1, 0), "reject wrong registry size");
    for (const LSTATUS status : {ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_MORE_DATA}) {
        Check(!DecodePreviewThemePreference(status, REG_DWORD, sizeof(DWORD), 0),
            "failed registry reads are unavailable");
    }

    auto registry = fallback;
    ApplyPreviewThemePreference(registry, {});
    Check(!registry.hasThemeColors, "missing preference does not confirm a theme");
    ApplyPreviewThemePreference(registry, true);
    Check(registry.hasThemeColors && registry.background == RGB(30, 30, 30) && registry.text == RGB(240, 240, 240),
        "registry dark fallback palette");
    auto registryLight = fallback;
    ApplyPreviewThemePreference(registryLight, false);
    Check(registryLight.hasThemeColors && registryLight.background == RGB(255, 255, 255) &&
            registryLight.text == RGB(0, 0, 0),
        "registry light fallback palette");
    ApplyPreviewThemePreference(registryLight, true);
    Check(registryLight.background == RGB(255, 255, 255), "registry does not replace an available UISettings palette");

    auto contrastFallback = fallback;
    contrastFallback.highContrast = true;
    ApplyPreviewThemePreference(contrastFallback, true);
    Check(!contrastFallback.hasThemeColors && contrastFallback.background == fallback.background,
        "registry does not replace high contrast system colors");

    auto transition = lightColors;
    Check(transition == ResolvePreviewTheme(light, {}, {}), "unchanged colors compare equal");
    Check(transition != darkColors, "light to dark changes effective colors");
    transition = darkColors;
    Check(transition != lightColors, "dark to light changes effective colors");

    {
        PreviewThemeWatcher watcher;
        watcher.Start(nullptr, WM_APP + 1);
        Check(!watcher.HasSubscription(), "invalid notification window leaves subscription unavailable");
        Check(watcher.Read().hasThemeColors, "theme colors remain usable without a subscription on this machine");
        watcher.Stop();
        watcher.Stop();
        Check(!watcher.HasSubscription() && !watcher.IsNotification(1), "stopped watcher rejects stale notifications");
    }
    Check(g_objectCount == 0, "watcher without subscription does not retain the module");

    HDC screen = GetDC(nullptr);
    Check(screen != nullptr, "buffer reference DC");
    const DWORD baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    {
        PreviewPaintBuffer buffer;
        Check(!buffer.Ensure(screen, 0, 30) && !buffer.DC(), "zero width needs no bitmap");
        Check(!buffer.Ensure(screen, 30, 0) && !buffer.DC(), "zero height needs no bitmap");
        Check(buffer.Ensure(screen, 80, 60), "allocate paint buffer");
        const HDC original = buffer.DC();
        Check(buffer.Ensure(screen, 80, 60) && buffer.DC() == original, "reuse unchanged buffer");
        for (int i = 0; i < 100; ++i) {
            Check(buffer.Ensure(screen, 80 + i, 60 + i), "resize paint buffer");
            SetPixel(buffer.DC(), 1, 1, RGB(17, 33, 65));
            Check(GetPixel(buffer.DC(), 1, 1) == RGB(17, 33, 65), "resized buffer is usable");
        }
        Check(!buffer.Ensure(nullptr, 80, 60) && !buffer.DC(), "buffer failure permits direct painting");
        Check(!buffer.Ensure(screen, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()) && !buffer.DC(),
            "failed bitmap allocation cleans up its DC");
        Check(buffer.Ensure(screen, 80, 60), "recover after buffer failure");
        buffer.Reset();
        buffer.Reset();
        Check(!buffer.DC(), "idempotent buffer cleanup");
    }
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baseline, "no GDI accumulation from buffers");
    ReleaseDC(nullptr, screen);
}

template <class T> struct Ptr {
    T* p = nullptr;

    ~Ptr() {
        if (p) {
            p->Release();
        }
    }

    T* operator->() const {
        return p;
    }

    Ptr() = default;

    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
};

using Bytes = std::vector<std::byte>;

void U16(Bytes& b, uint16_t value) {
    b.push_back(std::byte(value >> 8));
    b.push_back(std::byte(value & 255));
}

void U32(Bytes& b, uint32_t value) {
    U16(b, uint16_t(value >> 16));
    U16(b, uint16_t(value));
}

void Name(Bytes& b, const std::wstring& name) {
    U16(b, uint16_t(name.size() + 1));

    for (auto c : name) {
        U16(b, uint16_t(c));
    }

    U16(b, 0);
}

Bytes Header(uint32_t count) {
    Bytes b;
    U32(b, 0x41534546);
    U32(b, 0x10000);
    U32(b, count);

    return b;
}

void Block(Bytes& b, uint16_t type, const Bytes& data) {
    U16(b, type);
    U32(b, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}

Bytes Color(const wchar_t* name, const char* model, std::initializer_list<float> components, uint16_t type = 2) {
    Bytes b;
    Name(b, name);

    for (unsigned i = 0; i < 4; ++i) {
        b.push_back(std::byte(model[i]));
    }

    for (auto value : components) {
        U32(b, std::bit_cast<uint32_t>(value));
    }

    U16(b, type);

    return b;
}

Bytes Sample() {
    auto b = Header(8);
    Bytes group;
    Name(group, L"Group \U0001f3a8");
    Block(b, 0xc001, group);

    Block(b, 1, Color(L"Red & RGB", "RGB ", {1, 0, 0}, 0));
    Block(b, 1, Color(L"Gray", "Gray", {0.5f}));
    Block(b, 1, Color(L"Cyan", "CMYK", {1, 0, 0, 0}, 1));
    Block(b, 1, Color(L"White", "LAB ", {1, 0, 0}));
    Block(b, 1, Color(L"Unknown", "XYZ ", {1, 0, 0}));

    Block(b, 0x7777, {std::byte{1}, std::byte{2}});
    Block(b, 0xc002, {});

    return b;
}

void Reject(const Bytes& bytes, const char* label) {
    bool rejected = false;

    try {
        (void)ase::Parse(bytes);
    }
    catch (const ase::ParseError&) {
        rejected = true;
    }

    Check(rejected, label);
}

void ParserTests() {
    // Establish expected output before exercising malformed input.
    const auto bytes = Sample();
    const auto palette = ase::Parse(bytes);
    Check(palette.colorCount == 5 && palette.entries.size() == 7, "palette/group count");
    Check(palette.entries[0].name == L"Group \U0001f3a8", "group name and surrogate pair");
    Check(palette.entries[1].rgb == std::array<uint8_t, 3>{255, 0, 0}, "RGB red");
    Check(palette.entries[2].rgb == std::array<uint8_t, 3>{128, 128, 128}, "Gray half");
    Check(palette.entries[3].rgb == std::array<uint8_t, 3>{0, 255, 255}, "CMYK cyan");
    Check(palette.entries[4].rgb == std::array<uint8_t, 3>{255, 255, 255}, "Lab D50 white");
    Check(!palette.entries[5].supported, "unknown model preserved");
    Check(ase::Describe(palette.entries[3]).find(L"Approximate sRGB rendering without an ICC profile") !=
            std::wstring::npos,
        "approximation tooltip");

    // Preserve user-provided Unicode names independently of the application's language.
    const std::wstring unicodeName = L"\u0413\u0440\u0443\u043f\u043f\u0430 \U0001f3a8";
    auto unicodeBytes = Header(1);
    Block(unicodeBytes, 1, Color(unicodeName.c_str(), "RGB ", {1, 0, 0}));
    const auto unicodePalette = ase::Parse(unicodeBytes);
    Check(unicodePalette.entries[0].name == unicodeName, "UTF16 Cyrillic and surrogate pair preserved");
    Check(ase::Describe(unicodePalette.entries[0]).starts_with(unicodeName + L"\n"), "Unicode tooltip name preserved");

    auto unnamed = palette.entries[1];
    unnamed.name.clear();
    Check(ase::Describe(unnamed).starts_with(L"Unnamed\n"), "English unnamed tooltip");
    Check(ase::Describe(palette.entries[5]).find(L"Unsupported color model") != std::wstring::npos,
        "English unsupported color model tooltip");
    for (uint16_t type = 0; type < 3; ++type) {
        const wchar_t* labels[] = {L"Global", L"Spot", L"Normal"};
        unnamed.colorType = type;
        Check(ase::Describe(unnamed).ends_with(std::wstring(L"\n") + labels[type]), "English color type tooltip");
    }

    Check(ase::Parse(Header(0)).colorCount == 0, "empty palette");

    for (size_t i = 0; i < bytes.size(); ++i) {
        Reject(Bytes(bytes.begin(), bytes.begin() + i), "truncated input");
    }

    // Each mutation isolates one header, structure or payload rejection.
    auto bad = bytes;
    bad[0] = std::byte{0};
    Reject(bad, "signature");

    bad = bytes;
    bad[7] = std::byte{1};
    Reject(bad, "unsupported version");

    bad = bytes;
    bad.push_back(std::byte{0});
    Reject(bad, "trailing bytes");

    bad = Header(ase::MaxBlocks + 1);
    Reject(bad, "block limit");

    bad = Header(1);
    U16(bad, 1);
    U32(bad, 0xffffffff);
    Reject(bad, "length overflow");

    bad = Header(1);
    Block(bad, 0xc002, {});
    Reject(bad, "unmatched group end");

    bad = Header(1);
    Bytes group;
    Name(group, L"x");
    Block(bad, 0xc001, group);
    Reject(bad, "unclosed group");

    bad = Header(1);
    Block(bad, 1, Color(L"x", "RGB ", {std::numeric_limits<float>::quiet_NaN(), 0, 0}));
    Reject(bad, "NaN");

    bad = Header(1);
    Block(bad, 1, Color(L"x", "RGB ", {std::numeric_limits<float>::infinity(), 0, 0}));
    Reject(bad, "infinity");

    bad = Header(1);
    Block(bad, 1, Color(L"x", "RGB ", {2, 0, 0}));
    Reject(bad, "range");

    bad = Header(1);
    Block(bad, 1, Color(L"x", "RGB ", {1, 0, 0}, 3));
    Reject(bad, "color type");

    bad = Header(1);
    auto invalidName = Color(L"x", "RGB ", {1, 0, 0});
    invalidName[2] = std::byte{0xdc};
    Block(bad, 1, invalidName);
    Reject(bad, "invalid UTF16");

    bad = Bytes(ase::MaxBytes + 1);
    Reject(bad, "byte limit");

    auto nested = Header(5);
    Block(nested, 0xc001, group);
    Block(nested, 0xc001, group);
    Block(nested, 1, Color(L"black", "LAB ", {0, 0, 0}));
    Block(nested, 0xc002, {});
    Block(nested, 0xc002, {});
    Check(ase::Parse(nested).entries[2].rgb == std::array<uint8_t, 3>{0, 0, 0}, "nested groups / Lab black");

    // Deterministic mutations probe parser robustness without changing the fixture.
    std::mt19937 random(12345);
    for (unsigned n = 0; n < 2000; ++n) {
        auto mutated = bytes;
        mutated[random() % mutated.size()] = std::byte(random() % 256);
        try {
            (void)ase::Parse(mutated);
        }
        catch (const ase::ParseError&) {
        }
    }

    std::cout << "Parser: boundary checks and 2000 deterministic mutations passed\n";
}

class ShortStream final : public IStream {
public:
    ShortStream(IStream* source, ULONG chunk, bool* destroyed = nullptr)
        : source(source), chunk(chunk), destroyed(destroyed) {
        source->AddRef();
    }

    ~ShortStream() {
        source->Release();
        if (destroyed) {
            *destroyed = true;
        }
    }

    bool noStat = false;
    bool noSeek = false;
    HRESULT readFailure = S_OK;
    ULONG calls = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) {
            return E_POINTER;
        }

        *result = nullptr;

        if (iid != IID_IUnknown && iid != IID_IStream && iid != IID_ISequentialStream) {
            return E_NOINTERFACE;
        }

        *result = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ULONG(InterlockedIncrement(&refs));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs);

        if (!n) {
            delete this;
        }

        return ULONG(n);
    }

    HRESULT STDMETHODCALLTYPE Read(void* data, ULONG size, ULONG* read) override {
        ++calls;
        if (FAILED(readFailure)) {
            return readFailure;
        }

        return source->Read(data, std::min(size, chunk), read);
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* position) override {
        return noSeek ? STG_E_INVALIDFUNCTION : source->Seek(offset, origin, position);
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Revert() override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD mode) override {
        return noStat ? E_NOTIMPL : source->Stat(stat, mode);
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream**) override {
        return E_NOTIMPL;
    }

private:
    LONG refs = 1;
    IStream* source;
    ULONG chunk;
    bool* destroyed;
};

void MakeStream(const Bytes& bytes, IStream** result) {
    Check(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, result)), "create memory stream");

    ULONG written = 0;
    Check(SUCCEEDED((*result)->Write(bytes.data(), ULONG(bytes.size()), &written)) && written == bytes.size(),
        "write memory stream");

    LARGE_INTEGER zero{};
    Check(SUCCEEDED((*result)->Seek(zero, STREAM_SEEK_SET, nullptr)), "rewind stream");
}

void StreamTests() {
    Ptr<IStream> source;
    const auto bytes = Sample();
    MakeStream(bytes, &source.p);

    Ptr<ShortStream> stream;
    stream.p = new ShortStream(source.p, 3);
    Bytes read;
    Check(SUCCEEDED(ase::ReadStream(stream.p, read)) && read == bytes, "short S_OK reads");

    stream->noStat = true;
    Check(SUCCEEDED(ase::ReadStream(stream.p, read)) && read == bytes, "stream without Stat");

    LARGE_INTEGER zero{};
    source->Seek(zero, STREAM_SEEK_SET, nullptr);
    stream->noSeek = true;
    Check(SUCCEEDED(ase::ReadStream(stream.p, read)) && read == bytes, "non-seekable stream");

    stream->readFailure = STG_E_READFAULT;
    Check(ase::ReadStream(stream.p, read) == STG_E_READFAULT, "read failure HRESULT");

    Check(ase::ReadStream(nullptr, read) == E_POINTER, "null stream");

    ULARGE_INTEGER oversized{};
    oversized.QuadPart = ase::MaxBytes + 1;
    Check(SUCCEEDED(source->SetSize(oversized)), "large stream size");
    stream->noStat = false;
    Check(ase::ReadStream(stream.p, read) == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), "stream size limit");

    std::cout << "Streams: short reads, unsupported Stat/Seek, read errors and size limit passed\n";
}

using GetClass = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using CanUnload = HRESULT(STDAPICALLTYPE*)();

struct Module {
    HMODULE handle;
    GetClass get;
    CanUnload unload;

    explicit Module(const wchar_t* path) {
        handle = LoadLibraryW(path);
        Check(handle != nullptr, "LoadLibrary DLL");

        get = reinterpret_cast<GetClass>(GetProcAddress(handle, "DllGetClassObject"));
        unload = reinterpret_cast<CanUnload>(GetProcAddress(handle, "DllCanUnloadNow"));
        Check(get && unload, "undecorated COM exports");
    }

    ~Module() {
        FreeLibrary(handle);
    }
};

class Frame final : public IPreviewHandlerFrame {
public:
    LONG refs = 1;
    unsigned calls = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) {
            return E_POINTER;
        }

        *result = nullptr;

        if (iid != IID_IUnknown && iid != IID_IPreviewHandlerFrame) {
            return E_NOINTERFACE;
        }

        *result = static_cast<IPreviewHandlerFrame*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ULONG(++refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return ULONG(--refs);
    }

    HRESULT STDMETHODCALLTYPE GetWindowContext(PREVIEWHANDLERFRAMEINFO*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*) override {
        ++calls;
        return S_OK;
    }
};

struct Host {
    HWND window;

    Host() {
        window = CreateWindowExW(0,
            L"STATIC",
            L"AsePreview test host",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            720,
            580,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        Check(window != nullptr, "create host HWND");
    }

    ~Host() {
        if (IsWindow(window)) {
            DestroyWindow(window);
        }
    }
};

void ComTests(const wchar_t* dll) {
    PreviewThemeWatcher theme;
    theme.Start(nullptr, WM_APP + 1);
    const auto expectedColors = ResolvePreviewTheme(theme.Read(), RGB(255, 255, 255), RGB(0, 0, 0));
    Module module(dll);
    Check(module.unload() == S_OK, "initial unload state");

    void* result = reinterpret_cast<void*>(1);
    const GUID unknown{0};
    Check(module.get(unknown, IID_IClassFactory, &result) == CLASS_E_CLASSNOTAVAILABLE && result == nullptr,
        "unknown CLSID");
    Check(module.get(CLSID_AsePreview, IID_IClassFactory, nullptr) == E_POINTER, "factory null output");

    {
        // Verify factory contracts before creating the preview handler.
        Ptr<IClassFactory> factory;
        Check(SUCCEEDED(module.get(CLSID_AsePreview, IID_PPV_ARGS(&factory.p))), "factory creation");
        Check(module.unload() == S_FALSE, "factory keeps DLL alive");

        factory->LockServer(TRUE);
        factory->LockServer(FALSE);

        Check(factory->CreateInstance(nullptr, IID_IPreviewHandler, nullptr) == E_POINTER, "object null output");
        result = reinterpret_cast<void*>(1);
        Check(factory->CreateInstance(factory.p, IID_IPreviewHandler, &result) == CLASS_E_NOAGGREGATION &&
                result == nullptr,
            "no aggregation");

        Host host, secondHost;
        Frame frame;

        {
            Ptr<IPreviewHandler> preview;
            Check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview.p))), "preview creation");

            Ptr<IInitializeWithStream> init;
            Ptr<IOleWindow> window;
            Ptr<IObjectWithSite> site;
            Ptr<IPreviewHandlerVisuals> visuals;
            Check(SUCCEEDED(preview->QueryInterface(IID_PPV_ARGS(&init.p))), "stream interface");
            Check(SUCCEEDED(preview->QueryInterface(IID_PPV_ARGS(&window.p))), "window interface");
            Check(SUCCEEDED(preview->QueryInterface(IID_PPV_ARGS(&site.p))), "site interface");
            Check(SUCCEEDED(preview->QueryInterface(IID_PPV_ARGS(&visuals.p))), "visuals interface");

            Ptr<IUnknown> identity1, identity2;
            Check(SUCCEEDED(init->QueryInterface(IID_PPV_ARGS(&identity1.p))) &&
                    SUCCEEDED(window->QueryInterface(IID_PPV_ARGS(&identity2.p))) && identity1.p == identity2.p,
                "IUnknown identity");

            result = reinterpret_cast<void*>(1);
            Check(preview->QueryInterface(unknown, &result) == E_NOINTERFACE && !result, "unknown interface");

            HWND hwnd = reinterpret_cast<HWND>(1);
            Check(window->GetWindow(&hwnd) == E_FAIL && !hwnd, "no HWND before DoPreview");
            Check(preview->DoPreview() == E_UNEXPECTED, "DoPreview requires initialization");
            Check(init->Initialize(nullptr, STGM_READ) == E_INVALIDARG, "null initialize");

            // Repeated site assignment must balance references and forward host accelerators.
            Check(site->SetSite(&frame) == S_OK && site->SetSite(&frame) == S_OK && frame.refs == 3,
                "repeat SetSite retains both references");

            MSG msg{};
            msg.message = WM_KEYDOWN;
            msg.wParam = VK_TAB;
            Check(preview->TranslateAccelerator(&msg) == S_OK && frame.calls == 1, "forward accelerator");
            Check(preview->TranslateAccelerator(nullptr) == E_INVALIDARG, "null accelerator");

            RECT rect{10, 15, 370, 315};
            Check(preview->SetWindow(host.window, &rect) == S_OK, "SetWindow");

            RECT invalid{0, 0, -1, 1};
            Check(preview->SetRect(&invalid) == E_INVALIDARG, "negative geometry");

            LOGFONTW font{};
            font.lfHeight = -18;
            wcscpy_s(font.lfFaceName, L"Segoe UI");
            Check(visuals->SetFont(&font) == S_OK && visuals->SetBackgroundColor(RGB(255, 255, 255)) == S_OK &&
                    visuals->SetTextColor(RGB(0, 0, 0)) == S_OK,
                "visuals before creation");

            // Initialize once and retain the stream for the preview lifetime.
            Ptr<IStream> source;
            MakeStream(Sample(), &source.p);
            bool destroyed = false;
            auto* stream = new ShortStream(source.p, 7, &destroyed);
            Check(init->Initialize(stream, STGM_READ) == S_OK, "initialize stream");
            Check(init->Initialize(stream, STGM_READ) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
                "double initialize rejected");
            Check(preview->DoPreview() == S_OK, "DoPreview real stream");
            const auto reads = stream->calls;
            Check(window->GetWindow(&hwnd) == S_OK && hwnd != host.window && GetParent(hwnd) == host.window,
                "own preview child HWND");

            {
                // Render off-screen so colors can be checked independently of a visible host.
                RECT client{};
                GetClientRect(hwnd, &client);
                HDC screen = GetDC(hwnd);
                HDC memory = CreateCompatibleDC(screen);
                HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
                Check(screen && memory && bitmap, "render test DC/bitmap");

                const auto oldBitmap = SelectObject(memory, bitmap);
                SendMessageW(hwnd, WM_PRINTCLIENT, WPARAM(memory), PRF_CLIENT);
                Check(GetPixel(memory, 1, 1) == expectedColors.background,
                    "Windows theme overrides Shell host background");
                SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
                SendMessageW(hwnd, WM_APP + 1, std::numeric_limits<WPARAM>::max(), 0);
                SendMessageW(hwnd, WM_PRINTCLIENT, WPARAM(memory), PRF_CLIENT);
                Check(GetPixel(memory, 1, 1) == expectedColors.background, "theme refresh retains Windows background");
                const UINT dpi = GetDpiForWindow(hwnd);
                Check(GetPixel(memory, MulDiv(20, int(dpi), 96), MulDiv(48, int(dpi), 96)) == RGB(255, 0, 0),
                    "RGB swatch painted red");

                SelectObject(memory, oldBitmap);
                DeleteObject(bitmap);
                DeleteDC(memory);
                ReleaseDC(hwnd, screen);
            }

            Check(preview->DoPreview() == S_OK && stream->calls == reads, "repeat DoPreview avoids duplicate parsing");
            HWND repeat = nullptr;
            window->GetWindow(&repeat);
            Check(repeat == hwnd, "repeat DoPreview reuses HWND");

            rect = {20, 25, 280, 230};
            Check(preview->SetWindow(secondHost.window, &rect) == S_OK && GetParent(hwnd) == secondHost.window,
                "reparent existing preview");
            RECT bounds{};
            GetWindowRect(hwnd, &bounds);
            Check(bounds.right - bounds.left == 260 && bounds.bottom - bounds.top == 205, "resize on SetWindow");

            rect.right = 340;
            Check(preview->SetRect(&rect) == S_OK, "SetRect live preview");
            GetWindowRect(hwnd, &bounds);
            Check(bounds.right - bounds.left == 320, "resize on SetRect");

            ::SetFocus(hwnd);
            HWND focus = nullptr;
            Check(preview->QueryFocus(&focus) == S_OK && focus == ::GetFocus(), "QueryFocus actual focus");
            Check(visuals->SetFont(&font) == S_OK, "visuals live preview");

            // Theme notifications retain Windows colors and do not replace the HWND or scrollbar.
            SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
            SendMessageW(hwnd, WM_SYSCOLORCHANGE, 0, 0);
            SendMessageW(hwnd, WM_SETTINGCHANGE, 0, 0);
            Check(IsWindow(hwnd) && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL),
                "theme keeps native scroll window");

            ValidateRect(hwnd, nullptr);
            SendMessageW(hwnd, WM_TIMER, 1, 0);
            Check(!GetUpdateRect(hwnd, nullptr, FALSE), "unchanged timer refresh does not invalidate preview");
            SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
            SendMessageW(hwnd, WM_SETTINGCHANGE, 0, 0);
            Check(!GetUpdateRect(hwnd, nullptr, FALSE), "unchanged theme notifications do not invalidate preview");

            // Exercise actual WM_PAINT as well as WM_PRINTCLIENT, without a visible host.
            InvalidateRect(hwnd, nullptr, FALSE);
            SendMessageW(hwnd, WM_PAINT, 0, 0);
            const DWORD paintBaseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (int i = 0; i < 40; ++i) {
                RECT resized{0, 0, 320 + i % 3, 205 + i % 7};
                Check(preview->SetRect(&resized) == S_OK, "repeated preview resize");
                SendMessageW(hwnd, WM_VSCROLL, i % 2 ? SB_BOTTOM : SB_TOP, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
                SendMessageW(hwnd, WM_PAINT, 0, 0);
            }
            RECT zero{};
            Check(preview->SetRect(&zero) == S_OK, "zero-sized preview");
            SendMessageW(hwnd, WM_PAINT, 0, 0);
            Check(preview->SetRect(&rect) == S_OK, "restore preview after zero size");
            SendMessageW(hwnd, WM_PAINT, 0, 0);
            Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == paintBaseline,
                "resizing and painting do not accumulate GDI resources");

            stream->Release();
            Check(!destroyed, "handler retains stream");

            Check(preview->Unload() == S_OK && destroyed && !IsWindow(hwnd),
                "Unload destroys window and releases stream");
            Check(preview->Unload() == S_OK && preview->DoPreview() == E_UNEXPECTED, "idempotent unload");
            Check(site->SetSite(nullptr) == S_OK && frame.refs == 1, "release site/frame");

            {
                // Exercise scroll state through Win32 messages and keyboard forwarding.
                auto many = Header(300);
                for (unsigned i = 0; i < 300; ++i) {
                    Block(many, 1, Color(L"swatch", "RGB ", {0, 1, 0}));
                }

                Ptr<IStream> scrolling;
                MakeStream(many, &scrolling.p);
                Check(init->Initialize(scrolling.p, STGM_READ) == S_OK &&
                        preview->SetWindow(host.window, &rect) == S_OK && preview->DoPreview() == S_OK,
                    "large scrolling palette");

                window->GetWindow(&hwnd);
                SCROLLINFO info{sizeof(info), SIF_ALL};
                GetScrollInfo(hwnd, SB_VERT, &info);
                Check(info.nMax > int(info.nPage) && info.nPos == 0, "scroll range and initial position");

                SendMessageW(hwnd, WM_VSCROLL, SB_BOTTOM, 0);
                GetScrollInfo(hwnd, SB_VERT, &info);
                Check(info.nPos > 0, "scroll to bottom");

                SendMessageW(hwnd, WM_VSCROLL, SB_TOP, 0);
                GetScrollInfo(hwnd, SB_VERT, &info);
                Check(info.nPos == 0, "scroll to top");

                SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
                GetScrollInfo(hwnd, SB_VERT, &info);
                Check(info.nPos > 0, "mouse wheel scroll");

                Check(preview->SetFocus() == S_OK, "SetFocus actual preview");
                msg.wParam = VK_HOME;
                Check(preview->TranslateAccelerator(&msg) == S_OK, "keyboard Home handled");
                GetScrollInfo(hwnd, SB_VERT, &info);
                Check(info.nPos == 0, "keyboard scroll to start");

                Check(preview->Unload() == S_OK, "scrolling preview unload");
            }

            // Warmup has completed: diagnostic previews must also release their GDI objects.
            const DWORD lifecycleBaseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (unsigned i = 0; i < 30; ++i) {
                Ptr<IStream> next;
                MakeStream(i % 2 ? Header(0) : Bytes{std::byte{0}}, &next.p);
                Check(init->Initialize(next.p, STGM_READ) == S_OK && preview->SetWindow(host.window, &rect) == S_OK &&
                        preview->DoPreview() == S_OK,
                    "empty/malformed content has a diagnostic window");
                window->GetWindow(&hwnd);
                SendMessageW(hwnd, WM_PAINT, 0, 0);
                SendMessageW(hwnd, WM_SETTINGCHANGE, 0, 0);
                Check(preview->Unload() == S_OK, "repeated preview lifecycle");
            }
            std::cout << "Lifecycle GDI: " << lifecycleBaseline << " -> "
                      << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << "\n";
            Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == lifecycleBaseline,
                "diagnostic preview lifecycle does not accumulate GDI resources");
        }

        Check(frame.refs == 1, "site references balanced");

        WNDCLASSW wc{};
        Check(!GetClassInfoW(module.handle, L"Aragajaga.AsePreviewWindow", &wc),
            "window class unregistered before DLL unload");

        // The server lock must keep the module alive independently of any factory.
        factory->LockServer(TRUE);
        factory.p->Release();
        factory.p = nullptr;
        Check(module.unload() == S_FALSE, "LockServer independently keeps DLL alive");

        Ptr<IClassFactory> unlock;
        Check(module.get(CLSID_AsePreview, IID_PPV_ARGS(&unlock.p)) == S_OK, "factory for unlock");
        unlock->LockServer(FALSE);
    }

    Check(module.unload() == S_OK, "all COM counts balanced");
    std::cout << "COM: exports, identity, lifetime, sites, HWND reuse/reparent/resize and diagnostics passed\n";
}

void RealFiles(const wchar_t* root) {
    size_t files = 0, colors = 0;
    for (const auto& path : std::filesystem::recursive_directory_iterator(root)) {
        if (!path.is_regular_file() || path.path().extension() != L".ase") {
            continue;
        }

        std::ifstream file(path.path(), std::ios::binary);
        std::vector<char> raw((std::istreambuf_iterator<char>(file)), {});
        const auto palette = ase::Parse(std::as_bytes(std::span(raw)));
        Check(palette.colorCount > 0, "real Illustrator palette");

        ++files;
        colors += palette.colorCount;
    }

    Check(files > 0, "at least one real ASE palette");
    std::cout << "Illustrator compatibility: " << files << " files, " << colors << " colors passed\n";
}

IPreviewHandler* visiblePreview = nullptr;

LRESULT CALLBACK VisibleProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_SIZE && visiblePreview) {
        RECT rect{};
        GetClientRect(window, &rect);
        visiblePreview->SetRect(&rect);
        return 0;
    }

    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }

    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wp, lp);
}

void Show(const wchar_t* dll, const wchar_t* file, std::optional<bool> dark = {}) {
    Module module(dll);
    Ptr<IClassFactory> factory;
    Check(module.get(CLSID_AsePreview, IID_PPV_ARGS(&factory.p)) == S_OK, "visible factory");
    Ptr<IPreviewHandler> preview;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&preview.p)) == S_OK, "visible preview");

    Ptr<IInitializeWithStream> init;
    preview->QueryInterface(IID_PPV_ARGS(&init.p));
    Ptr<IStream> stream;
    Check(SUCCEEDED(SHCreateStreamOnFileEx(file, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream.p)),
        "open visible sample");
    init->Initialize(stream.p, STGM_READ);

    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = VisibleProc;
    wc.lpszClassName = L"AsePreview.TestHost";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const HWND host = CreateWindowExW(0,
        wc.lpszClassName,
        L"AsePreview test host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        580,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);
    Check(host != nullptr, "visible host");

    RECT rect{};
    GetClientRect(host, &rect);
    preview->SetWindow(host, &rect);
    Check(preview->DoPreview() == S_OK, "visible DoPreview");
    if (dark) {
        Ptr<IPreviewHandlerVisuals> visuals;
        Check(preview->QueryInterface(IID_PPV_ARGS(&visuals.p)) == S_OK, "visible host visuals");
        visuals->SetBackgroundColor(*dark ? RGB(30, 30, 30) : RGB(255, 255, 255));
        visuals->SetTextColor(*dark ? RGB(240, 240, 240) : RGB(0, 0, 0));
    }
    visiblePreview = preview.p;

    // Launchers can hide the console via STARTUPINFO. A second call displays the host itself.
    ShowWindow(host, SW_SHOW);
    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (preview->TranslateAccelerator(&msg) != S_OK) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    visiblePreview = nullptr;
    preview->Unload();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "Usage: AsePreviewTests DLL [IllustratorPresets] | DLL --show[|-dark|-light] sample.ase\n";
        return 2;
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        return 2;
    }

    int status = 0;
    try {
        if (argc == 3 && std::wstring(argv[2]) == L"--clipboard-test") {
            SwatchClipboardTests();
            std::cout << "PASS: " << checks << " clipboard checks\n";
        }
        else if (argc == 4 &&
            (std::wstring(argv[2]) == L"--show" || std::wstring(argv[2]) == L"--show-dark" ||
                std::wstring(argv[2]) == L"--show-light")) {
            const std::wstring mode = argv[2];
            Show(argv[1],
                argv[3],
                mode == L"--show" ? std::optional<bool>{} : std::optional<bool>{mode == L"--show-dark"});
        }
        else {
            SwatchCopyTests();
            ThemeAndBufferTests();
            ParserTests();
            StreamTests();
            ComTests(argv[1]);

            // Verify class registration does not survive a complete unload/reload of the DLL.
            ComTests(argv[1]);
            APTTYPE apartment;
            APTTYPEQUALIFIER qualifier;
            Check(CoGetApartmentType(&apartment, &qualifier) == S_OK &&
                    (apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA),
                "preview preserves host STA apartment");

            // WinRT initialization must also respect a host that already uses the MTA.
            std::exception_ptr threadError;
            std::thread mtaHost([&]() {
                const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                try {
                    Check(SUCCEEDED(hr), "initialize MTA test host");
                    ComTests(argv[1]);
                    Check(CoGetApartmentType(&apartment, &qualifier) == S_OK && apartment == APTTYPE_MTA,
                        "preview preserves host MTA apartment");
                }
                catch (...) {
                    threadError = std::current_exception();
                }
                if (SUCCEEDED(hr)) {
                    CoUninitialize();
                }
            });
            mtaHost.join();
            if (threadError) {
                std::rethrow_exception(threadError);
            }

            if (argc >= 3) {
                RealFiles(argv[2]);
            }

            std::cout << "PASS: " << checks << " checks\n";
        }
    }
    catch (const ase::ParseError& error) {
        std::wcerr << L"FAIL: " << error.message << L"\n";
        status = 1;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        status = 1;
    }

    CoUninitialize();
    return status;
}
