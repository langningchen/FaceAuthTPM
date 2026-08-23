// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <credentialprovider.h>
#include <string>
#include <atomic>

class FaceCredential final : public ICredentialProviderCredential2 {
public:
    FaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,std::wstring sid,std::wstring qualifiedName,std::wstring displayName);
    const std::wstring& Sid() const { return sid_; }

    IFACEMETHODIMP QueryInterface(REFIID riid,void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;
    IFACEMETHODIMP Advise(ICredentialProviderCredentialEvents* events) override;
    IFACEMETHODIMP UnAdvise() override;
    IFACEMETHODIMP SetSelected(BOOL* autoLogon) override;
    IFACEMETHODIMP SetDeselected() override;
    IFACEMETHODIMP GetFieldState(DWORD,CREDENTIAL_PROVIDER_FIELD_STATE*,CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE*) override;
    IFACEMETHODIMP GetStringValue(DWORD,PWSTR*) override;
    IFACEMETHODIMP GetBitmapValue(DWORD,HBITMAP*) override;
    IFACEMETHODIMP GetCheckboxValue(DWORD,BOOL*,PWSTR*) override;
    IFACEMETHODIMP GetSubmitButtonValue(DWORD,DWORD*) override;
    IFACEMETHODIMP GetComboBoxValueCount(DWORD,DWORD*,DWORD*) override;
    IFACEMETHODIMP GetComboBoxValueAt(DWORD,DWORD,PWSTR*) override;
    IFACEMETHODIMP SetStringValue(DWORD,PCWSTR) override;
    IFACEMETHODIMP SetCheckboxValue(DWORD,BOOL) override;
    IFACEMETHODIMP SetComboBoxSelectedValue(DWORD,DWORD) override;
    IFACEMETHODIMP CommandLinkClicked(DWORD) override;
    IFACEMETHODIMP GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE*,CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*,PWSTR*,CREDENTIAL_PROVIDER_STATUS_ICON*) override;
    IFACEMETHODIMP ReportResult(NTSTATUS,NTSTATUS,PWSTR*,CREDENTIAL_PROVIDER_STATUS_ICON*) override;
    IFACEMETHODIMP GetUserSid(PWSTR*) override;
private:
    ~FaceCredential();
    std::atomic<ULONG> ref_{1};
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_;
    std::wstring sid_,qualifiedName_,displayName_;
    ICredentialProviderCredentialEvents* events_{};
};
