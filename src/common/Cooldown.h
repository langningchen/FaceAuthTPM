// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace faceauth {
void SetCooldown(const std::wstring& sid, unsigned seconds = 60);
bool IsInCooldown(const std::wstring& sid);
void ClearCooldown(const std::wstring& sid);
}
