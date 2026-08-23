// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <string_view>

namespace faceauth {
inline constexpr wchar_t kProductName[] = L"FaceAuth TPM";
inline constexpr wchar_t kDataFolder[] = L"FaceAuth";
inline constexpr wchar_t kTpmKeyName[] = L"FaceAuth.TPM.Vault.RSA.v1";
inline constexpr wchar_t kCredentialExtension[] = L".fav";
inline constexpr wchar_t kProfileExtension[] = L".fap";
inline constexpr wchar_t kIdentityExtension[] = L".fai";
inline constexpr DWORD kCooldownSeconds = 60;
inline constexpr double kMatchThreshold = 0.45;
inline constexpr double kMatchMargin = 0.055;
inline constexpr int kRequiredConsecutiveMatches = 3;
inline constexpr int kSensorTimeoutMs = 10000;
inline constexpr int kSensorStartupGraceMs = 7000;
}
