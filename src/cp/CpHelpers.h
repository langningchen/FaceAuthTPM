// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <string>
#include <windows.h>

namespace faceauth {
bool IsPhysicalConsoleSession();
bool IsFaceAuthSessionAllowed();
HRESULT RetrieveNegotiateAuthPackage(ULONG* packageId);
std::wstring RunFaceSensor(void* moduleHandle, int timeoutMs,
                           const std::atomic<bool>* cancelRequested = nullptr);
HRESULT DupString(PCWSTR src, PWSTR* dst);
} // namespace faceauth
