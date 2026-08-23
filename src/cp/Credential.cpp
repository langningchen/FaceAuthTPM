// SPDX-License-Identifier: GPL-3.0-only
#include "cp/Credential.h"
#include "cp/CpHelpers.h"
#include "cp/Guid.h"
#include "common/TpmVault.h"
#include "common/Cooldown.h"
#include <wincred.h>
#include <shlwapi.h>
#include <string>

extern void DllAddRef();
extern void DllRelease();

enum : DWORD { FID_TITLE=0,FID_STATUS=1,FID_COUNT=2 };

FaceCredential::FaceCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,std::wstring sid,std::wstring q,std::wstring d)
    :cpus_(cpus),sid_(std::move(sid)),qualifiedName_(std::move(q)),displayName_(std::move(d)){ DllAddRef(); }
FaceCredential::~FaceCredential(){ if(events_) events_->Release(); DllRelease(); }
HRESULT FaceCredential::QueryInterface(REFIID riid,void** ppv){
    if(!ppv)return E_INVALIDARG; *ppv=nullptr;
    if(riid==IID_IUnknown||riid==IID_ICredentialProviderCredential||riid==IID_ICredentialProviderCredential2) *ppv=static_cast<ICredentialProviderCredential2*>(this);
    else return E_NOINTERFACE; AddRef(); return S_OK;
}
ULONG FaceCredential::AddRef(){return ++ref_;} ULONG FaceCredential::Release(){ULONG r=--ref_;if(!r)delete this;return r;}
HRESULT FaceCredential::Advise(ICredentialProviderCredentialEvents* e){ if(events_)events_->Release(); events_=e; if(events_)events_->AddRef(); return S_OK; }
HRESULT FaceCredential::UnAdvise(){ if(events_){events_->Release();events_=nullptr;} return S_OK; }
HRESULT FaceCredential::SetSelected(BOOL* a){if(!a)return E_INVALIDARG;*a=FALSE;return S_OK;} HRESULT FaceCredential::SetDeselected(){return S_OK;}
HRESULT FaceCredential::GetFieldState(DWORD id,CREDENTIAL_PROVIDER_FIELD_STATE* s,CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* i){
    if(!s||!i||id>=FID_COUNT)return E_INVALIDARG;
    *s=(id==FID_STATUS?CPFS_DISPLAY_IN_SELECTED_TILE:CPFS_DISPLAY_IN_BOTH);
    *i=CPFIS_NONE;
    return S_OK;
}
HRESULT FaceCredential::GetStringValue(DWORD id,PWSTR* out){
    if(id==FID_TITLE){std::wstring s=L"FaceAuth — "+displayName_;return faceauth::DupString(s.c_str(),out);}
    if(id==FID_STATUS)return faceauth::DupString(L"Look at the camera. FaceAuth uses the preloaded recognition service and starts scanning immediately.",out);
    return E_INVALIDARG;
}
HRESULT FaceCredential::GetBitmapValue(DWORD,HBITMAP* bitmap){
    if(!bitmap)return E_INVALIDARG;
    *bitmap=nullptr;
    return E_NOTIMPL;
}
HRESULT FaceCredential::GetCheckboxValue(DWORD,BOOL*,PWSTR*){return E_NOTIMPL;}
HRESULT FaceCredential::GetSubmitButtonValue(DWORD,DWORD*){return E_NOTIMPL;} HRESULT FaceCredential::GetComboBoxValueCount(DWORD,DWORD*,DWORD*){return E_NOTIMPL;}
HRESULT FaceCredential::GetComboBoxValueAt(DWORD,DWORD,PWSTR*){return E_NOTIMPL;} HRESULT FaceCredential::SetStringValue(DWORD,PCWSTR){return E_NOTIMPL;}
HRESULT FaceCredential::SetCheckboxValue(DWORD,BOOL){return E_NOTIMPL;} HRESULT FaceCredential::SetComboBoxSelectedValue(DWORD,DWORD){return E_NOTIMPL;}
HRESULT FaceCredential::CommandLinkClicked(DWORD){return E_NOTIMPL;}

HRESULT FaceCredential::GetSerialization(CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* ser,PWSTR* status,CREDENTIAL_PROVIDER_STATUS_ICON* icon){
    if(!response||!ser||!status||!icon)return E_INVALIDARG; *response=CPGSR_NO_CREDENTIAL_NOT_FINISHED; *status=nullptr; *icon=CPSI_NONE; ZeroMemory(ser,sizeof(*ser));
    if((cpus_!=CPUS_LOGON&&cpus_!=CPUS_UNLOCK_WORKSTATION)||!faceauth::IsFaceAuthSessionAllowed())return E_ACCESSDENIED;
    if(faceauth::IsInCooldown(sid_)){ faceauth::DupString(L"Face sign-in is cooling down after a failed attempt. Use Password or Windows Hello.",status); *icon=CPSI_WARNING; return S_OK; }
    faceauth::SecurePassword password; std::wstring error;
    if(!faceauth::DecryptPasswordForSid(sid_,password,&error)){ faceauth::DupString((L"FaceAuth TPM error: "+error).c_str(),status); *icon=CPSI_ERROR; return S_OK; }
    DWORD flags=CRED_PACK_PROTECTED_CREDENTIALS|CRED_PACK_ID_PROVIDER_CREDENTIALS; DWORD cb=0;
    BOOL packed=CredPackAuthenticationBufferW(flags,const_cast<PWSTR>(qualifiedName_.c_str()),const_cast<PWSTR>(password.c_str()),nullptr,&cb);
    if(packed||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||cb==0){ faceauth::DupString(L"Could not prepare Microsoft account credential.",status); *icon=CPSI_ERROR; return S_OK; }
    ser->rgbSerialization=static_cast<BYTE*>(CoTaskMemAlloc(cb)); if(!ser->rgbSerialization)return E_OUTOFMEMORY; ser->cbSerialization=cb;
    if(!CredPackAuthenticationBufferW(flags,const_cast<PWSTR>(qualifiedName_.c_str()),const_cast<PWSTR>(password.c_str()),ser->rgbSerialization,&ser->cbSerialization)){
        CoTaskMemFree(ser->rgbSerialization); ZeroMemory(ser,sizeof(*ser)); faceauth::DupString(L"Could not pack Microsoft account credential.",status); *icon=CPSI_ERROR; return S_OK;
    }
    ULONG package=0; HRESULT hr=faceauth::RetrieveNegotiateAuthPackage(&package);
    if(FAILED(hr)){ CoTaskMemFree(ser->rgbSerialization); ZeroMemory(ser,sizeof(*ser)); return hr; }
    ser->ulAuthenticationPackage=package; ser->clsidCredentialProvider=CLSID_FaceAuthCredentialProvider; *response=CPGSR_RETURN_CREDENTIAL_FINISHED; return S_OK;
}

HRESULT FaceCredential::ReportResult(NTSTATUS status,NTSTATUS,PWSTR* text,CREDENTIAL_PROVIDER_STATUS_ICON* icon){
    if(!text||!icon)return E_INVALIDARG; *text=nullptr; *icon=CPSI_NONE;
    if(status<0){ faceauth::SetCooldown(sid_); faceauth::DupString(L"Face sign-in failed. Use Password/Windows Hello. If your Microsoft password changed, run FaceAuthEnroll again.",text); *icon=CPSI_ERROR; }
    else faceauth::ClearCooldown(sid_);
    return S_OK;
}
HRESULT FaceCredential::GetUserSid(PWSTR* sid){return faceauth::DupString(sid_.c_str(),sid);}
