// SPDX-License-Identifier: GPL-3.0-only
#include "common/IdentityRecord.h"
#include "common/Paths.h"
#include "common/Security.h"
#include <windows.h>
#include <ntsecapi.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cwctype>

namespace faceauth {
namespace {
constexpr uint32_t kMagic = 0x31494146; // FAI1
constexpr uint32_t kVersion = 1;
constexpr uint32_t kFlagOnlineIdentity = 0x1;

#pragma pack(push,1)
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t sidChars;
    uint32_t qualifiedChars;
    uint32_t displayChars;
};
#pragma pack(pop)

std::wstring FromLsaString(const LSA_UNICODE_STRING& s) {
    if (!s.Buffer || s.Length == 0) return {};
    return std::wstring(s.Buffer, s.Length / sizeof(wchar_t));
}

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    if (!b || a.size() != wcslen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

bool ContainsNoCase(const std::wstring& value, const wchar_t* needle) {
    if (!needle || !*needle) return true;
    std::wstring a=value, b=needle;
    for (auto& c:a) c=static_cast<wchar_t>(std::towlower(c));
    for (auto& c:b) c=static_cast<wchar_t>(std::towlower(c));
    return a.find(b) != std::wstring::npos;
}

bool LookupSidName(const std::wstring& sidString, std::wstring& qualified, std::wstring& display) {
    PSID sid=nullptr;
    if (!ConvertStringSidToSidW(sidString.c_str(), &sid)) return false;
    DWORD nameChars=0, domainChars=0; SID_NAME_USE use{};
    LookupAccountSidW(nullptr, sid, nullptr, &nameChars, nullptr, &domainChars, &use);
    if (GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !nameChars) { LocalFree(sid); return false; }
    std::vector<wchar_t> name(nameChars), domain(domainChars ? domainChars : 1);
    if (!LookupAccountSidW(nullptr, sid, name.data(), &nameChars, domain.data(), &domainChars, &use)) {
        LocalFree(sid); return false;
    }
    LocalFree(sid);
    display.assign(name.data());
    if (domainChars && domain[0]) qualified=std::wstring(domain.data())+L"\\"+name.data();
    else qualified.assign(name.data());
    return true;
}

bool CurrentLogonSessionStrings(std::wstring& user, std::wstring& domain,
                                std::wstring& upn, std::wstring& authPackage) {
    HANDLE token=nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_STATISTICS stats{}; DWORD cb=0;
    const BOOL ok=GetTokenInformation(token, TokenStatistics, &stats, sizeof(stats), &cb);
    CloseHandle(token);
    if (!ok) return false;

    PSECURITY_LOGON_SESSION_DATA data=nullptr;
    if (LsaGetLogonSessionData(&stats.AuthenticationId, &data) != 0 || !data) return false;
    user=FromLsaString(data->UserName);
    domain=FromLsaString(data->LogonDomain);
    upn=FromLsaString(data->Upn);
    authPackage=FromLsaString(data->AuthenticationPackage);
    LsaFreeReturnBuffer(data);
    return true;
}

bool IsLikelyOnlineQualifiedName(const std::wstring& q) {
    const auto slash=q.find(L'\\');
    return slash!=std::wstring::npos && q.find(L'@', slash+1)!=std::wstring::npos;
}
}

bool SaveIdentityRecord(const IdentityRecord& identity, std::wstring* error) {
    if (identity.sid.empty() || identity.qualifiedName.empty() || identity.sid.size()>256 ||
        identity.qualifiedName.size()>1024 || identity.displayName.size()>1024) {
        if (error) *error=L"Invalid identity metadata";
        return false;
    }
    if (!EnsureDataDirectories()) { if(error)*error=L"Cannot create FaceAuth data directories"; return false; }
    Header h{kMagic,kVersion,identity.onlineIdentity?kFlagOnlineIdentity:0,
             static_cast<uint32_t>(identity.sid.size()),
             static_cast<uint32_t>(identity.qualifiedName.size()),
             static_cast<uint32_t>(identity.displayName.size())};
    auto path=IdentityPath(identity.sid), tmp=path; tmp+=L".tmp";
    std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
    if(!f){if(error)*error=L"Cannot create identity metadata";return false;}
    f.write(reinterpret_cast<const char*>(&h),sizeof(h));
    f.write(reinterpret_cast<const char*>(identity.sid.data()),identity.sid.size()*sizeof(wchar_t));
    f.write(reinterpret_cast<const char*>(identity.qualifiedName.data()),identity.qualifiedName.size()*sizeof(wchar_t));
    f.write(reinterpret_cast<const char*>(identity.displayName.data()),identity.displayName.size()*sizeof(wchar_t));
    f.close();
    if(!f || !ApplyAdminSystemOnlyAcl(tmp)){std::filesystem::remove(tmp);if(error)*error=L"Writing/securing identity metadata failed";return false;}
    std::error_code ec; std::filesystem::rename(tmp,path,ec);
    if(ec){std::filesystem::remove(path,ec);ec.clear();std::filesystem::rename(tmp,path,ec);}
    if(ec){if(error)*error=L"Replacing identity metadata failed";return false;}
    ApplyAdminSystemOnlyAcl(path);
    return true;
}

bool LoadIdentityRecord(const std::wstring& sid, IdentityRecord& identity, std::wstring* error) {
    std::ifstream f(IdentityPath(sid),std::ios::binary);
    if(!f){if(error)*error=L"Identity metadata not found";return false;}
    Header h{};f.read(reinterpret_cast<char*>(&h),sizeof(h));
    if(!f||h.magic!=kMagic||h.version!=kVersion||h.sidChars>256||h.qualifiedChars==0||h.qualifiedChars>1024||h.displayChars>1024){
        if(error)*error=L"Invalid identity metadata header";return false;
    }
    std::wstring fileSid(h.sidChars,L'\0'), qualified(h.qualifiedChars,L'\0'), display(h.displayChars,L'\0');
    f.read(reinterpret_cast<char*>(fileSid.data()),fileSid.size()*sizeof(wchar_t));
    f.read(reinterpret_cast<char*>(qualified.data()),qualified.size()*sizeof(wchar_t));
    if(!display.empty()) f.read(reinterpret_cast<char*>(display.data()),display.size()*sizeof(wchar_t));
    if(!f||fileSid!=sid){if(error)*error=L"Identity metadata SID mismatch/truncated";return false;}
    identity={std::move(fileSid),std::move(qualified),std::move(display),(h.flags&kFlagOnlineIdentity)!=0};
    if(identity.displayName.empty()) identity.displayName=identity.qualifiedName;
    return true;
}

std::vector<IdentityRecord> LoadAllIdentityRecords() {
    std::vector<IdentityRecord> out; std::error_code ec;
    if(!std::filesystem::exists(IdentitiesDir(),ec)) return out;
    for(const auto& e:std::filesystem::directory_iterator(IdentitiesDir(),ec)){
        if(ec)break;
        if(!e.is_regular_file()||e.path().extension()!=L".fai")continue;
        IdentityRecord r; if(LoadIdentityRecord(e.path().stem().wstring(),r,nullptr))out.push_back(std::move(r));
    }
    return out;
}

bool DeleteIdentityRecord(const std::wstring& sid){std::error_code ec;return !std::filesystem::exists(IdentityPath(sid),ec)||std::filesystem::remove(IdentityPath(sid),ec);}
bool IdentityRecordExists(const std::wstring& sid){std::error_code ec;return std::filesystem::is_regular_file(IdentityPath(sid),ec);}

bool BuildCurrentIdentityRecord(IdentityRecord& identity,const std::wstring& qualifiedOverride,
                                const std::wstring& displayOverride,std::wstring* error) {
    identity={};
    identity.sid=CurrentUserSidString();
    if(identity.sid.empty()){if(error)*error=L"Could not determine current user SID";return false;}

    std::wstring user,domain,upn,authPackage;
    CurrentLogonSessionStrings(user,domain,upn,authPackage);

    if(!qualifiedOverride.empty()) {
        identity.qualifiedName=qualifiedOverride;
        identity.onlineIdentity=IsLikelyOnlineQualifiedName(qualifiedOverride);
    } else if(EqualsNoCase(domain,L"MicrosoftAccount")) {
        std::wstring account=(upn.find(L'@')!=std::wstring::npos)?upn:user;
        if(!account.empty()) identity.qualifiedName=domain+L"\\"+account;
        identity.onlineIdentity=true;
    } else if(!upn.empty() && upn.find(L'@')!=std::wstring::npos &&
              (ContainsNoCase(authPackage,L"CloudAP") || ContainsNoCase(domain,L"AzureAD"))) {
        const std::wstring provider=domain.empty()?L"MicrosoftAccount":domain;
        identity.qualifiedName=provider+L"\\"+upn;
        identity.onlineIdentity=true;
    }

    std::wstring lookupQualified,lookupDisplay;
    LookupSidName(identity.sid,lookupQualified,lookupDisplay);
    if(identity.qualifiedName.empty()) identity.qualifiedName=lookupQualified;
    if(!qualifiedOverride.empty() && IsLikelyOnlineQualifiedName(qualifiedOverride)) identity.onlineIdentity=true;
    if(identity.qualifiedName.empty()){if(error)*error=L"Could not determine the qualified Windows account name";return false;}

    if(!displayOverride.empty()) identity.displayName=displayOverride;
    else if(identity.onlineIdentity){
        const auto slash=identity.qualifiedName.find(L'\\');
        identity.displayName=(slash==std::wstring::npos)?identity.qualifiedName:identity.qualifiedName.substr(slash+1);
    } else if(!lookupDisplay.empty()) identity.displayName=lookupDisplay;
    else identity.displayName=identity.qualifiedName;

    return true;
}

} // namespace faceauth
