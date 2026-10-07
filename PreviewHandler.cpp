#include "pch.h"
#include "PreviewHandler.h"
#include "AseStream.h"
#include "Module.h"
#include <new>

namespace {

bool ValidRect(const RECT& rect) {
    // Compute in 64 bits before passing dimensions to Win32's signed int parameters.
    const auto width = int64_t(rect.right) - rect.left;
    const auto height = int64_t(rect.bottom) - rect.top;
    return width >= 0 && height >= 0 && width <= INT_MAX && height <= INT_MAX;
}

} // namespace

PreviewHandler::PreviewHandler() noexcept {
    InterlockedIncrement(&g_objectCount);
}

PreviewHandler::~PreviewHandler() {
    Unload();

    if (m_frame) {
        m_frame->Release();
    }
    if (m_site) {
        m_site->Release();
    }

    InterlockedDecrement(&g_objectCount);
}

HRESULT STDMETHODCALLTYPE PreviewHandler::QueryInterface(REFIID iid, void** result) {
    if (!result) {
        return E_POINTER;
    }

    *result = nullptr;

    if (iid == IID_IUnknown || iid == IID_IPreviewHandler) {
        *result = static_cast<IPreviewHandler*>(this);
    }
    else if (iid == IID_IInitializeWithStream) {
        *result = static_cast<IInitializeWithStream*>(this);
    }
    else if (iid == IID_IObjectWithSite) {
        *result = static_cast<IObjectWithSite*>(this);
    }
    else if (iid == IID_IOleWindow) {
        *result = static_cast<IOleWindow*>(this);
    }
    else if (iid == IID_IPreviewHandlerVisuals) {
        *result = static_cast<IPreviewHandlerVisuals*>(this);
    }
    else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE PreviewHandler::AddRef() {
    return ULONG(InterlockedIncrement(&m_refCount));
}

ULONG STDMETHODCALLTYPE PreviewHandler::Release() {
    const LONG refs = InterlockedDecrement(&m_refCount);

    if (!refs) {
        delete this;
    }

    return ULONG(refs);
}

HRESULT STDMETHODCALLTYPE PreviewHandler::Initialize(IStream* stream, DWORD mode) {
    UNREFERENCED_PARAMETER(mode);

    if (!stream) {
        return E_INVALIDARG;
    }

    if (m_stream) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }

    m_stream = stream;
    m_stream->AddRef();
    m_loaded = false;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetWindow(HWND window, const RECT* rect) {
    if (!IsWindow(window) || !rect || !ValidRect(*rect)) {
        return E_INVALIDARG;
    }

    m_parentWindow = window;
    m_rect = *rect;

    return m_preview ? m_preview->Place(window, *rect) : S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetRect(const RECT* rect) {
    if (!rect || !ValidRect(*rect)) {
        return E_INVALIDARG;
    }

    m_rect = *rect;

    return m_preview ? m_preview->Place(m_parentWindow, *rect) : S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::DoPreview() {
    if (!m_stream || !IsWindow(m_parentWindow)) {
        return E_UNEXPECTED;
    }

    try {
        // A recreated window must receive the host's visuals before loading content.
        if (!m_preview || !m_preview->Handle()) {
            m_preview = std::make_unique<AsePreviewWindow>();
            m_preview->SetColors(m_background, m_text);
            const HRESULT create = m_preview->Create(m_parentWindow, m_rect);
            if (FAILED(create)) {
                m_preview.reset();
                return create;
            }

            if (m_hasFont) {
                const HRESULT font = m_preview->SetFont(m_font);
                if (FAILED(font)) {
                    m_preview.reset();
                    return font;
                }
            }

            m_loaded = false;
        }

        if (m_loaded) {
            return S_OK;
        }

        // Report input failures in the preview; allocation failures retain their HRESULT.
        std::vector<std::byte> bytes;
        const HRESULT read = ase::ReadStream(m_stream, bytes);

        if (FAILED(read)) {
            if (read == E_OUTOFMEMORY) {
                return read;
            }

            m_preview->SetContent({},
                read == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                    ? L"The ASE file exceeds the 64 MiB limit."
                    : L"Unable to read the ASE file.");
        }
        else {
            try {
                m_preview->SetContent(ase::Parse(bytes), nullptr);
            }
            catch (const ase::ParseError& error) {
                m_preview->SetContent({}, error.message);
            }
        }

        m_loaded = true;
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_FAIL;
    }
}

HRESULT STDMETHODCALLTYPE PreviewHandler::Unload() {
    m_preview.reset();
    m_loaded = false;

    if (m_stream) {
        m_stream->Release();
        m_stream = nullptr;
    }

    m_parentWindow = nullptr;
    m_rect = {};

    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetFocus() {
    if (!m_preview || !m_preview->Handle()) {
        return E_UNEXPECTED;
    }

    ::SetFocus(m_preview->Handle());
    return ::GetFocus() == m_preview->Handle() ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::QueryFocus(HWND* window) {
    if (!window) {
        return E_POINTER;
    }

    *window = ::GetFocus();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::TranslateAccelerator(MSG* message) {
    if (!message) {
        return E_INVALIDARG;
    }

    if (m_preview && m_preview->HandleKey(message)) {
        return S_OK;
    }

    return m_frame ? m_frame->TranslateAccelerator(message) : S_FALSE;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::GetWindow(HWND* window) {
    if (!window) {
        return E_POINTER;
    }

    *window = m_preview ? m_preview->Handle() : nullptr;
    return *window ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::ContextSensitiveHelp(BOOL) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetSite(IUnknown* site) {
    // Retain first: the caller may pass the same site again.
    IPreviewHandlerFrame* frame = nullptr;
    if (site) {
        site->AddRef();
        site->QueryInterface(IID_PPV_ARGS(&frame));
    }

    if (m_frame) {
        m_frame->Release();
    }
    if (m_site) {
        m_site->Release();
    }

    m_site = site;
    m_frame = frame;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::GetSite(REFIID iid, void** result) {
    if (!result) {
        return E_POINTER;
    }

    *result = nullptr;

    return m_site ? m_site->QueryInterface(iid, result) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetBackgroundColor(COLORREF color) {
    m_background = color;
    if (m_preview) {
        m_preview->SetColors(m_background, m_text);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetTextColor(COLORREF color) {
    m_text = color;
    if (m_preview) {
        m_preview->SetColors(m_background, m_text);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE PreviewHandler::SetFont(const LOGFONTW* font) {
    if (!font) {
        return E_POINTER;
    }

    try {
        if (m_preview) {
            const HRESULT hr = m_preview->SetFont(*font);
            if (FAILED(hr)) {
                return hr;
            }
        }

        m_font = *font;
        m_hasFont = true;
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_FAIL;
    }
}
