// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace faceauth {
bool ApplyAdminSystemOnlyAcl(const std::filesystem::path& path);
bool IsProcessElevated();
bool IsRunningAsLocalSystem();
std::wstring CurrentUserSidString();
std::wstring ActiveConsoleUserSidString();
void SecureErase(std::vector<unsigned char>& v);
void SecureErase(std::vector<wchar_t>& v);
} // namespace faceauth
