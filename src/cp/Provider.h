// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <credentialprovider.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

class FaceCredential;

class FaceProvider final : public ICredentialProvider {
public:
    FaceProvider();
    IFACEMETHODIMP QueryInterface(REFIID riid,void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,DWORD flags) override;
    IFACEMETHODIMP SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) override;
    IFACEMETHODIMP Advise(ICredentialProviderEvents* events,UINT_PTR context) override;
    IFACEMETHODIMP UnAdvise() override;
    IFACEMETHODIMP GetFieldDescriptorCount(DWORD* count) override;
    IFACEMETHODIMP GetFieldDescriptorAt(DWORD index,CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** field) override;
    IFACEMETHODIMP GetCredentialCount(DWORD* count,DWORD* defaultIndex,BOOL* autoLogonWithDefault) override;
    IFACEMETHODIMP GetCredentialAt(DWORD index,ICredentialProviderCredential** credential) override;
private:
    ~FaceProvider();
    void ClearCredentials();
    void RebuildCredentials();
    bool HasCredentialForSid(const std::wstring& sid);
    void MaybeStartRecognition();
    static DWORD WINAPI RecognitionThread(void* context);
    void RecognitionWorker();

    std::atomic<ULONG> ref_{1};
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_{CPUS_INVALID};
    DWORD flags_{};
    std::vector<FaceCredential*> credentials_;
    std::atomic<bool> recognitionStarted_{false};
    std::wstring matchedSid_;
    std::atomic<bool> autoArmed_{false};
    std::mutex credentialMutex_;
    std::mutex eventMutex_;
    ICredentialProviderEvents* events_{};
    UINT_PTR adviseContext_{};
};
