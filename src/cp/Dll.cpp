// SPDX-License-Identifier: GPL-3.0-only
#include "cp/Guid.h"
#include "cp/Provider.h"
#include <atomic>
#include <new>
#include <shlwapi.h>
#include <string>
#include <windows.h>

// {4CF34D82-0D5D-4A5C-9E46-64C16F348C62}
const CLSID CLSID_FaceAuthCredentialProvider = {
    0x4cf34d82, 0x0d5d, 0x4a5c, {0x9e, 0x46, 0x64, 0xc1, 0x6f, 0x34, 0x8c, 0x62}};
HMODULE g_faceAuthModule = nullptr;
static std::atomic<long> g_refs{0};
void DllAddRef() { ++g_refs; }
void DllRelease() { --g_refs; }

class Factory final : public IClassFactory {
  public:
    Factory() { DllAddRef(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override {
        if (!p)
            return E_INVALIDARG;
        *p = nullptr;
        if (r == IID_IUnknown || r == IID_IClassFactory)
            *p = static_cast<IClassFactory*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref_;
        if (!r)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto* p = new (std::nothrow) FaceProvider();
        if (!p)
            return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock)
            DllAddRef();
        else
            DllRelease();
        return S_OK;
    }

  private:
    ~Factory() { DllRelease(); }
    std::atomic<ULONG> ref_{1};
};

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_faceAuthModule = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
STDAPI DllCanUnloadNow(void) { return g_refs.load() == 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) {
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_FaceAuthCredentialProvider)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) Factory();
    if (!factory)
        return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

namespace {
std::wstring GuidString() {
    wchar_t b[64]{};
    StringFromGUID2(CLSID_FaceAuthCredentialProvider, b, 64);
    return b;
}
HRESULT SetDefault(HKEY root, const std::wstring& path, const std::wstring& value) {
    HKEY k = nullptr;
    LONG e = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (e != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(e);
    e = RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return HRESULT_FROM_WIN32(e);
}
} // namespace

STDAPI DllRegisterServer(void) {
    wchar_t module[32768]{};
    DWORD n = GetModuleFileNameW(g_faceAuthModule, module, 32768);
    if (!n || n >= 32768)
        return HRESULT_FROM_WIN32(GetLastError());
    std::wstring g = GuidString();
    std::wstring cls = L"SOFTWARE\\Classes\\CLSID\\" + g;
    HRESULT hr = SetDefault(HKEY_LOCAL_MACHINE, cls, L"FaceAuth TPM Credential Provider");
    if (FAILED(hr))
        return hr;
    hr = SetDefault(HKEY_LOCAL_MACHINE, cls + L"\\InprocServer32", module);
    if (FAILED(hr))
        return hr;
    HKEY k = nullptr;
    LONG e =
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, (cls + L"\\InprocServer32").c_str(), 0, KEY_WRITE, &k);
    if (e != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(e);
    const wchar_t apartment[] = L"Apartment";
    e = RegSetValueExW(k, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(apartment),
                       sizeof(apartment));
    RegCloseKey(k);
    if (e != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(e);
    return SetDefault(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\" + g,
        L"FaceAuth TPM");
}
STDAPI DllUnregisterServer(void) {
    std::wstring g = GuidString();
    RegDeleteTreeW(
        HKEY_LOCAL_MACHINE,
        (L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\Credential Providers\\" +
         g)
            .c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\Classes\\CLSID\\" + g).c_str());
    return S_OK;
}
