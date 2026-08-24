// SPDX-License-Identifier: GPL-3.0-only
#include "cp/Provider.h"
#include "common/Constants.h"
#include "common/Cooldown.h"
#include "common/IdentityRecord.h"
#include "common/Paths.h"
#include "common/Security.h"
#include "common/TpmVault.h"
#include "cp/CpHelpers.h"
#include "cp/Credential.h"
#include <filesystem>
#include <new>
#include <string>

extern HMODULE g_faceAuthModule;
extern void DllAddRef();
extern void DllRelease();

enum : DWORD { FID_LOGO = 0, FID_TITLE = 1, FID_STATUS = 2, FID_COUNT = 3 };
static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR kFields[FID_COUNT] = {
    {FID_LOGO, CPFT_TILE_IMAGE, const_cast<PWSTR>(L"FaceAuth"), CPFG_CREDENTIAL_PROVIDER_LOGO},
    {FID_TITLE, CPFT_LARGE_TEXT, const_cast<PWSTR>(L"FaceAuth"), GUID_NULL},
    {FID_STATUS, CPFT_SMALL_TEXT, const_cast<PWSTR>(L"Status"), GUID_NULL}};

namespace {
void AppendProviderLog(const wchar_t* stage) {
    const auto root = faceauth::ProgramDataRoot();
    if (root.empty())
        return;
    HANDLE h = CreateFileW((root / L"provider-startup.log").c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t wide[320]{};
    int chars =
        swprintf_s(wide, L"[%04u-%02u-%02uT%02u:%02u:%02u.%03uZ] tick=%llu pid=%lu %ls\r\n",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                   static_cast<unsigned long long>(GetTickCount64()), GetCurrentProcessId(), stage);
    if (chars > 0) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, chars, nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string utf8(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, chars, utf8.data(), bytes, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
    CloseHandle(h);
}

void AppendCredentialCountLog(size_t count, const wchar_t* scope) {
    wchar_t message[128]{};
    swprintf_s(message, L"credentials_built count=%zu scope=%ls", count,
               scope ? scope : L"unknown");
    AppendProviderLog(message);
}

HRESULT CopyField(const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& in,
                  CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** out) {
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    auto* p = static_cast<CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*>(
        CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)));
    if (!p)
        return E_OUTOFMEMORY;
    *p = in;
    p->pszLabel = nullptr;
    HRESULT hr = faceauth::DupString(in.pszLabel, &p->pszLabel);
    if (FAILED(hr)) {
        CoTaskMemFree(p);
        return hr;
    }
    *out = p;
    return S_OK;
}
} // namespace

FaceProvider::FaceProvider() { DllAddRef(); }
FaceProvider::~FaceProvider() {
    UnAdvise();
    ClearCredentials();
    DllRelease();
}

HRESULT FaceProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv)
        return E_INVALIDARG;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ICredentialProvider)
        *ppv = static_cast<ICredentialProvider*>(this);
    else
        return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
ULONG FaceProvider::AddRef() { return ++ref_; }
ULONG FaceProvider::Release() {
    ULONG r = --ref_;
    if (!r)
        delete this;
    return r;
}

HRESULT FaceProvider::SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD flags) {
    cpus_ = cpus;
    flags_ = flags;
    if (cpus != CPUS_LOGON && cpus != CPUS_UNLOCK_WORKSTATION)
        return E_NOTIMPL;
    if (!faceauth::IsFaceAuthSessionAllowed())
        return E_NOTIMPL;

    AppendProviderLog(L"SetUsageScenario_allowed");
    {
        std::scoped_lock lock(credentialMutex_);
        matchedSid_.clear();
        autoArmed_ = false;
    }

    // v0.5: credentials are built directly from FaceAuth's enrollment metadata.
    // We intentionally do not implement ICredentialProviderSetUserArray, so LogonUI
    // has no SetUserArray synchronization point to wait on before GetCredentialCount.
    RebuildCredentials();
    MaybeStartRecognition();
    return S_OK;
}

HRESULT FaceProvider::SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) {
    return E_NOTIMPL;
}

HRESULT FaceProvider::Advise(ICredentialProviderEvents* e, UINT_PTR c) {
    {
        std::scoped_lock lock(eventMutex_);
        if (events_)
            events_->Release();
        events_ = e;
        adviseContext_ = c;
        if (events_)
            events_->AddRef();
    }
    bool matched = false;
    {
        std::scoped_lock lock(credentialMutex_);
        matched = autoArmed_.load() && !matchedSid_.empty();
    }
    if (matched && e)
        e->CredentialsChanged(c);
    else
        MaybeStartRecognition();
    return S_OK;
}

HRESULT FaceProvider::UnAdvise() {
    std::scoped_lock lock(eventMutex_);
    if (events_) {
        events_->Release();
        events_ = nullptr;
    }
    adviseContext_ = 0;
    return S_OK;
}
HRESULT FaceProvider::GetFieldDescriptorCount(DWORD* c) {
    if (!c)
        return E_INVALIDARG;
    *c = FID_COUNT;
    return S_OK;
}
HRESULT FaceProvider::GetFieldDescriptorAt(DWORD i, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** f) {
    if (i >= FID_COUNT)
        return E_INVALIDARG;
    return CopyField(kFields[i], f);
}

HRESULT FaceProvider::GetCredentialCount(DWORD* count, DWORD* def, BOOL* autoLogon) {
    if (!count || !def || !autoLogon)
        return E_INVALIDARG;
    std::scoped_lock lock(credentialMutex_);
    *count = static_cast<DWORD>(credentials_.size());
    *def = CREDENTIAL_PROVIDER_NO_DEFAULT;
    *autoLogon = FALSE;
    if (autoArmed_.exchange(false) && !matchedSid_.empty() &&
        !faceauth::IsInCooldown(matchedSid_)) {
        for (size_t i = 0; i < credentials_.size(); ++i) {
            if (credentials_[i]->Sid() == matchedSid_) {
                *def = static_cast<DWORD>(i);
                *autoLogon = TRUE;
                break;
            }
        }
    }
    return S_OK;
}

HRESULT FaceProvider::GetCredentialAt(DWORD i, ICredentialProviderCredential** c) {
    if (!c)
        return E_INVALIDARG;
    std::scoped_lock lock(credentialMutex_);
    if (i >= credentials_.size())
        return E_INVALIDARG;
    *c = credentials_[i];
    (*c)->AddRef();
    return S_OK;
}

void FaceProvider::ClearCredentials() {
    std::vector<FaceCredential*> old;
    {
        std::scoped_lock lock(credentialMutex_);
        old.swap(credentials_);
    }
    for (auto* c : old)
        c->Release();
}

void FaceProvider::RebuildCredentials() {
    std::vector<FaceCredential*> fresh;
    const auto identities = faceauth::LoadAllIdentityRecords();
    std::wstring unlockSid;
    if (cpus_ == CPUS_UNLOCK_WORKSTATION)
        unlockSid = faceauth::ActiveConsoleUserSidString();

    for (const auto& identity : identities) {
        if (identity.sid.empty() || identity.qualifiedName.empty() || !identity.onlineIdentity)
            continue;
        if (!faceauth::CredentialExists(identity.sid))
            continue;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(faceauth::ProfilePath(identity.sid), ec))
            continue;
        if (cpus_ == CPUS_UNLOCK_WORKSTATION) {
            if (unlockSid.empty() || identity.sid != unlockSid)
                continue;
        }
        auto* credential = new (std::nothrow)
            FaceCredential(cpus_, identity.sid, identity.qualifiedName, identity.displayName);
        if (credential)
            fresh.push_back(credential);
    }

    std::vector<FaceCredential*> old;
    {
        std::scoped_lock lock(credentialMutex_);
        old.swap(credentials_);
        credentials_.swap(fresh);
    }
    for (auto* c : old)
        c->Release();

    size_t count = 0;
    {
        std::scoped_lock lock(credentialMutex_);
        count = credentials_.size();
    }
    AppendCredentialCountLog(count, cpus_ == CPUS_UNLOCK_WORKSTATION ? L"unlock-current-user"
                                                                     : L"logon-enrolled-users");
}

bool FaceProvider::HasCredentialForSid(const std::wstring& sid) {
    std::scoped_lock lock(credentialMutex_);
    for (auto* c : credentials_)
        if (c->Sid() == sid)
            return true;
    return false;
}

void FaceProvider::MaybeStartRecognition() {
    if ((cpus_ != CPUS_LOGON && cpus_ != CPUS_UNLOCK_WORKSTATION) ||
        !faceauth::IsFaceAuthSessionAllowed())
        return;
    {
        std::scoped_lock lock(credentialMutex_);
        if (credentials_.empty())
            return;
        if (autoArmed_.load() && !matchedSid_.empty())
            return;
    }
    bool expected = false;
    if (!recognitionStarted_.compare_exchange_strong(expected, true))
        return;
    AppendProviderLog(L"recognition_thread_create");
    AddRef();
    HANDLE h = CreateThread(nullptr, 0, &FaceProvider::RecognitionThread, this, 0, nullptr);
    if (h)
        CloseHandle(h);
    else {
        recognitionStarted_ = false;
        Release();
    }
}

DWORD WINAPI FaceProvider::RecognitionThread(void* p) {
    static_cast<FaceProvider*>(p)->RecognitionWorker();
    static_cast<FaceProvider*>(p)->Release();
    return 0;
}

void FaceProvider::RecognitionWorker() {
    for (;;) {
        if ((cpus_ != CPUS_LOGON && cpus_ != CPUS_UNLOCK_WORKSTATION) ||
            !faceauth::IsFaceAuthSessionAllowed())
            break;
        AppendProviderLog(L"sensor_spawn_begin");
        std::wstring sid = faceauth::RunFaceSensor(g_faceAuthModule, faceauth::kSensorTimeoutMs);
        AppendProviderLog(sid.empty() ? L"sensor_return_empty" : L"sensor_return_match");
        if (sid.empty()) {
            bool advised = false;
            {
                std::scoped_lock lock(eventMutex_);
                advised = (events_ != nullptr);
            }
            if (!advised)
                break;
            Sleep(1500);
            continue;
        }
        if (!HasCredentialForSid(sid)) {
            bool advised = false;
            {
                std::scoped_lock lock(eventMutex_);
                advised = (events_ != nullptr);
            }
            if (!advised)
                break;
            Sleep(500);
            continue;
        }
        if (faceauth::IsInCooldown(sid))
            break;

        {
            std::scoped_lock lock(credentialMutex_);
            matchedSid_ = sid;
            autoArmed_ = true;
        }
        ICredentialProviderEvents* ev = nullptr;
        UINT_PTR ctx = 0;
        {
            std::scoped_lock lock(eventMutex_);
            ev = events_;
            ctx = adviseContext_;
            if (ev)
                ev->AddRef();
        }
        if (ev) {
            ev->CredentialsChanged(ctx);
            ev->Release();
        }
        break;
    }
    recognitionStarted_ = false;
}
