// SPDX-License-Identifier: GPL-3.0-only
#include "common/Paths.h"
#include "common/Constants.h"
#include "common/Security.h"
#include <shlobj.h>
#include <vector>
#include <windows.h>

namespace faceauth {
std::filesystem::path ProgramDataRoot() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &p)))
        return {};
    std::filesystem::path out(p);
    CoTaskMemFree(p);
    return out / kDataFolder;
}
std::filesystem::path CredentialsDir() { return ProgramDataRoot() / L"credentials"; }
std::filesystem::path ProfilesDir() { return ProgramDataRoot() / L"profiles"; }
std::filesystem::path IdentitiesDir() { return ProgramDataRoot() / L"identities"; }
std::filesystem::path ModelsDir() { return ProgramDataRoot() / L"models"; }
std::filesystem::path CooldownDir() { return ProgramDataRoot() / L"cooldown"; }
std::filesystem::path VaultPublicKeyPath() { return ProgramDataRoot() / L"vault-public.blob"; }
std::filesystem::path CredentialPath(const std::wstring& sid) {
    return CredentialsDir() / (sid + kCredentialExtension);
}
std::filesystem::path ProfilePath(const std::wstring& sid) {
    return ProfilesDir() / (sid + kProfileExtension);
}
std::filesystem::path IdentityPath(const std::wstring& sid) {
    return IdentitiesDir() / (sid + kIdentityExtension);
}
std::filesystem::path CooldownPath(const std::wstring& sid) {
    return CooldownDir() / (sid + L".txt");
}
std::filesystem::path ModuleDirectory(void* moduleHandle) {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(static_cast<HMODULE>(moduleHandle), buf.data(),
                                 static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size())
        return {};
    return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
}
bool EnsureDataDirectories() {
    try {
        for (const auto& p : {ProgramDataRoot(), CredentialsDir(), ProfilesDir(), IdentitiesDir(),
                              ModelsDir(), CooldownDir()}) {
            std::filesystem::create_directories(p);
            if (!ApplyAdminSystemOnlyAcl(p))
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace faceauth
