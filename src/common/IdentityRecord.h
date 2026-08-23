// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>

namespace faceauth {

struct IdentityRecord {
    std::wstring sid;
    std::wstring qualifiedName;
    std::wstring displayName;
    bool onlineIdentity{false};
};

bool SaveIdentityRecord(const IdentityRecord& identity, std::wstring* error = nullptr);
bool LoadIdentityRecord(const std::wstring& sid, IdentityRecord& identity, std::wstring* error = nullptr);
std::vector<IdentityRecord> LoadAllIdentityRecords();
bool DeleteIdentityRecord(const std::wstring& sid);
bool IdentityRecordExists(const std::wstring& sid);

// Builds identity metadata for the current interactive user. qualifiedOverride is
// useful for Microsoft accounts if Windows does not expose the online provider
// name through the current logon session. Example:
//   MicrosoftAccount\person@example.com
bool BuildCurrentIdentityRecord(
    IdentityRecord& identity,
    const std::wstring& qualifiedOverride = {},
    const std::wstring& displayOverride = {},
    std::wstring* error = nullptr);

} // namespace faceauth
