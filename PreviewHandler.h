#pragma once
#include <Windows.h>
#include <ShObjIdl.h>
#include <oleidl.h>
#include <memory>
#include "AsePreviewWindow.h"

class PreviewHandler final
    : public IPreviewHandler,
      public IInitializeWithStream,
      public IObjectWithSite,
      public IOleWindow,
      public IPreviewHandlerVisuals {
public:
    PreviewHandler() noexcept;
    ~PreviewHandler();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE Initialize(IStream*, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetWindow(HWND, const RECT*) override;
    HRESULT STDMETHODCALLTYPE SetRect(const RECT*) override;
    HRESULT STDMETHODCALLTYPE DoPreview() override;
    HRESULT STDMETHODCALLTYPE Unload() override;

    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE QueryFocus(HWND*) override;
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*) override;

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown*) override;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID, void**) override;

    HRESULT STDMETHODCALLTYPE GetWindow(HWND*) override;
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override;

    HRESULT STDMETHODCALLTYPE SetBackgroundColor(COLORREF) override;
    HRESULT STDMETHODCALLTYPE SetFont(const LOGFONTW*) override;
    HRESULT STDMETHODCALLTYPE SetTextColor(COLORREF) override;

private:
    LONG m_refCount = 1;

    IStream* m_stream = nullptr;
    IUnknown* m_site = nullptr;
    IPreviewHandlerFrame* m_frame = nullptr;

    HWND m_parentWindow = nullptr;
    RECT m_rect{};
    std::unique_ptr<AsePreviewWindow> m_preview;

    std::optional<COLORREF> m_background;
    std::optional<COLORREF> m_text;
    LOGFONTW m_font{};
    bool m_hasFont = false;

    bool m_loaded = false;
};
