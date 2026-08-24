// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>

namespace faceauth {
std::filesystem::path ProgramDataRoot();
std::filesystem::path CredentialsDir();
std::filesystem::path ProfilesDir();
std::filesystem::path IdentitiesDir();
std::filesystem::path ModelsDir();
std::filesystem::path CooldownDir();
std::filesystem::path VaultPublicKeyPath();
std::filesystem::path CredentialPath(const std::wstring& sid);
std::filesystem::path ProfilePath(const std::wstring& sid);
std::filesystem::path IdentityPath(const std::wstring& sid);
std::filesystem::path CooldownPath(const std::wstring& sid);
std::filesystem::path ModuleDirectory(void* moduleHandle);
bool EnsureDataDirectories();
} // namespace faceauth
