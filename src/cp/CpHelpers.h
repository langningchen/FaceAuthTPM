// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <string>

namespace faceauth {
bool IsPhysicalConsoleSession();
bool IsFaceAuthSessionAllowed();
HRESULT RetrieveNegotiateAuthPackage(ULONG* packageId);
std::wstring RunFaceSensor(void* moduleHandle, int timeoutMs);
HRESULT DupString(PCWSTR src, PWSTR* dst);
}
