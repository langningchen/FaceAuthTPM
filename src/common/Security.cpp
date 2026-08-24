// SPDX-License-Identifier: GPL-3.0-only
#include "common/Security.h"
#include <aclapi.h>
#include <sddl.h>
#include <vector>
#include <windows.h>
#include <wtsapi32.h>

namespace faceauth {
bool ApplyAdminSystemOnlyAcl(const std::filesystem::path& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    // Directories must propagate the SYSTEM/Administrators ACL to children.
    // Files use explicit non-inheritable ACEs. Older builds omitted OI/CI on
    // directories, which could make a later elevated reinstall unable to
    // replace model files created under a different ACL.
    std::error_code ec;
    const bool isDir = std::filesystem::is_directory(path, ec);
    const wchar_t* sddl =
        isDir ? L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)" : L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr))
        return false;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    bool ok = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted) && present;
    if (ok) {
        DWORD e =
            SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, dacl, nullptr);
        ok = (e == ERROR_SUCCESS);
    }
    LocalFree(sd);
    return ok;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD cb = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &cb);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

bool IsRunningAsLocalSystem() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD cb = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &cb);
    if (!cb) {
        CloseHandle(token);
        return false;
    }
    std::vector<unsigned char> buf(cb);
    if (!GetTokenInformation(token, TokenUser, buf.data(), cb, &cb)) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    BYTE systemSid[SECURITY_MAX_SID_SIZE]{};
    DWORD sidSize = sizeof(systemSid);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &sidSize))
        return false;
    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
    return EqualSid(tu->User.Sid, systemSid) == TRUE;
}

std::wstring CurrentUserSidString() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD cb = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &cb);
    std::vector<unsigned char> buf(cb);
    if (!GetTokenInformation(token, TokenUser, buf.data(), cb, &cb)) {
        CloseHandle(token);
        return {};
    }
    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
    LPWSTR sid = nullptr;
    std::wstring out;
    if (ConvertSidToStringSidW(tu->User.Sid, &sid)) {
        out = sid;
        LocalFree(sid);
    }
    CloseHandle(token);
    return out;
}

std::wstring ActiveConsoleUserSidString() {
    const DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF)
        return {};

    // LogonUI runs as LocalSystem, so this is the most direct and ambiguity-free
    // way to obtain the user SID of the locked console session.
    HANDLE token = nullptr;
    if (WTSQueryUserToken(sessionId, &token)) {
        DWORD cb = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &cb);
        if (cb) {
            std::vector<unsigned char> buf(cb);
            if (GetTokenInformation(token, TokenUser, buf.data(), cb, &cb)) {
                auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                LPWSTR sidText = nullptr;
                std::wstring out;
                if (ConvertSidToStringSidW(tu->User.Sid, &sidText)) {
                    out = sidText;
                    LocalFree(sidText);
                }
                if (!out.empty()) {
                    CloseHandle(token);
                    return out;
                }
            }
        }
        CloseHandle(token);
    }

    // Fallback for environments where WTSQueryUserToken is restricted.
    LPWSTR user = nullptr, domain = nullptr;
    DWORD userBytes = 0, domainBytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &user,
                                     &userBytes) ||
        !user || !*user) {
        if (user)
            WTSFreeMemory(user);
        return {};
    }
    WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSDomainName, &domain,
                                &domainBytes);
    std::wstring qualified =
        (domain && *domain) ? (std::wstring(domain) + L"\\" + user) : std::wstring(user);
    WTSFreeMemory(user);
    if (domain)
        WTSFreeMemory(domain);

    DWORD sidBytes = 0, nameBytes = 0;
    SID_NAME_USE use{};
    LookupAccountNameW(nullptr, qualified.c_str(), nullptr, &sidBytes, nullptr, &nameBytes, &use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !sidBytes)
        return {};
    std::vector<unsigned char> sid(sidBytes);
    std::vector<wchar_t> resolvedDomain(nameBytes ? nameBytes : 1);
    if (!LookupAccountNameW(nullptr, qualified.c_str(), sid.data(), &sidBytes,
                            resolvedDomain.data(), &nameBytes, &use))
        return {};
    LPWSTR sidText = nullptr;
    std::wstring out;
    if (ConvertSidToStringSidW(sid.data(), &sidText)) {
        out = sidText;
        LocalFree(sidText);
    }
    return out;
}

void SecureErase(std::vector<unsigned char>& v) {
    if (!v.empty())
        SecureZeroMemory(v.data(), v.size());
    v.clear();
    v.shrink_to_fit();
}
void SecureErase(std::vector<wchar_t>& v) {
    if (!v.empty())
        SecureZeroMemory(v.data(), v.size() * sizeof(wchar_t));
    v.clear();
    v.shrink_to_fit();
}
} // namespace faceauth
