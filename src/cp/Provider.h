// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "cp/MatchState.h"
#include <atomic>
#include <credentialprovider.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

class FaceCredential;

enum CREDENTIAL_PROVIDER_DISPLAY_STATE_FLAGS : DWORD {
    CPDSF_NONE = 0x00000000,
    CPDSF_AWAKE = 0x00000001,
    CPDSF_CURTAIN = 0x00000004,
    CPDSF_CURTAIN_MOVING = 0x00000008,
    CPDSF_STATION_LOCKED = 0x00000040,
    CPDSF_PROVIDER_VISIBLE = 0x00000080,
};

MIDL_INTERFACE("A09BCC29-D779-4513-BB59-B4DB5D82D2B6")
ICredentialProviderWithDisplayState : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetDisplayState(DWORD flags) = 0;
};
class FaceProvider final : public ICredentialProvider, public ICredentialProviderWithDisplayState {
  public:
    FaceProvider();
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, DWORD flags) override;
    IFACEMETHODIMP SetSerialization(const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) override;
    IFACEMETHODIMP Advise(ICredentialProviderEvents* events, UINT_PTR context) override;
    IFACEMETHODIMP UnAdvise() override;
    IFACEMETHODIMP GetFieldDescriptorCount(DWORD* count) override;
    IFACEMETHODIMP GetFieldDescriptorAt(DWORD index,
                                        CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** field) override;
    IFACEMETHODIMP GetCredentialCount(DWORD* count, DWORD* defaultIndex,
                                      BOOL* autoLogonWithDefault) override;
    IFACEMETHODIMP GetCredentialAt(DWORD index,
                                   ICredentialProviderCredential** credential) override;

    IFACEMETHODIMP SetDisplayState(DWORD flags) override;

  private:
    ~FaceProvider();
    void ClearCredentials();
    void RebuildCredentials();
    bool HasCredentialForSid(const std::wstring& sid);
    void MaybeStartRecognition();
    static DWORD WINAPI RecognitionThread(void* context);
    void RecognitionWorker();
    void StartCurtainWatchdog();
    static DWORD WINAPI CurtainWatchdogThread(void* context);
    void CurtainWatchdog();

    std::atomic<ULONG> ref_{1};
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_{CPUS_INVALID};
    DWORD flags_{};
    std::vector<FaceCredential*> credentials_;
    std::atomic<bool> recognitionStarted_{false};
    std::shared_ptr<faceauth::MatchState> matchState_ = std::make_shared<faceauth::MatchState>();
    std::atomic<bool> curtainActive_{true};
    std::atomic<bool> sawDisplayState_{false};
    std::mutex credentialMutex_;
    std::mutex eventMutex_;
    ICredentialProviderEvents* events_{};
    UINT_PTR adviseContext_{};
};
