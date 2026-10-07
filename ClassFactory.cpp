#include "pch.h"

#include "ClassFactory.h"
#include "PreviewHandler.h"
#include "Module.h"
#include <new>

ClassFactory::ClassFactory() {
    InterlockedIncrement(&g_objectCount);
}

ClassFactory::~ClassFactory() {
    InterlockedDecrement(&g_objectCount);
}

HRESULT STDMETHODCALLTYPE ClassFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppvObject = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ClassFactory::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE ClassFactory::Release() {
    const ULONG refCount = InterlockedDecrement(&m_refCount);

    if (refCount == 0) {
        delete this;
    }

    return refCount;
}

HRESULT STDMETHODCALLTYPE ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    *ppvObject = nullptr;

    if (pUnkOuter) {
        return CLASS_E_NOAGGREGATION;
    }

    auto* object = new (std::nothrow) PreviewHandler();

    if (!object) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = object->QueryInterface(riid, ppvObject);

    object->Release();

    return hr;
}

HRESULT STDMETHODCALLTYPE ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        InterlockedIncrement(&g_lockCount);
    }
    else {
        InterlockedDecrement(&g_lockCount);
    }

    return S_OK;
}
