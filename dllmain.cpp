// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <new>

#include "ClassFactory.h"
#include "Guids.h"

LONG g_objectCount = 0;
LONG g_lockCount = 0;
HINSTANCE g_hInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }

    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }

    *ppv = nullptr;

    if (rclsid != CLSID_AsePreview) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new (std::nothrow) ClassFactory();

    if (!factory) {
        return E_OUTOFMEMORY;
    }

    const HRESULT hr = factory->QueryInterface(riid, ppv);

    factory->Release();

    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    if (InterlockedCompareExchange(&g_objectCount, 0, 0) == 0 && InterlockedCompareExchange(&g_lockCount, 0, 0) == 0) {
        return S_OK;
    }

    return S_FALSE;
}
