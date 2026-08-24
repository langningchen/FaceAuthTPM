// SPDX-License-Identifier: GPL-3.0-only
#include "common/FaceProfile.h"
#include "common/Paths.h"
#include "common/Security.h"
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace faceauth {
namespace {
constexpr uint32_t kMagic = 0x31504146; // FAP1
#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t sidChars;
    uint32_t dimensions;
};
#pragma pack(pop)
} // namespace

bool SaveFaceProfile(const FaceProfile& profile, std::wstring* error) {
    if (profile.sid.empty() || profile.embedding.empty() || profile.embedding.size() > 4096) {
        if (error)
            *error = L"Invalid face profile";
        return false;
    }
    if (!EnsureDataDirectories()) {
        if (error)
            *error = L"Cannot create FaceAuth data directories";
        return false;
    }
    Header h{kMagic, 1, static_cast<uint32_t>(profile.sid.size()),
             static_cast<uint32_t>(profile.embedding.size())};
    auto path = ProfilePath(profile.sid), tmp = path;
    tmp += L".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error)
            *error = L"Cannot create profile file";
        return false;
    }
    f.write(reinterpret_cast<const char*>(&h), sizeof(h));
    f.write(reinterpret_cast<const char*>(profile.sid.data()),
            profile.sid.size() * sizeof(wchar_t));
    f.write(reinterpret_cast<const char*>(profile.embedding.data()),
            profile.embedding.size() * sizeof(float));
    f.close();
    if (!f) {
        std::filesystem::remove(tmp);
        if (error)
            *error = L"Writing profile failed";
        return false;
    }
    if (!ApplyAdminSystemOnlyAcl(tmp)) {
        std::filesystem::remove(tmp);
        if (error)
            *error = L"Securing profile ACL failed";
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        if (error)
            *error = L"Replacing profile failed";
        return false;
    }
    ApplyAdminSystemOnlyAcl(path);
    return true;
}

bool LoadFaceProfile(const std::wstring& sid, FaceProfile& profile, std::wstring* error) {
    std::ifstream f(ProfilePath(sid), std::ios::binary);
    if (!f) {
        if (error)
            *error = L"Profile not found";
        return false;
    }
    Header h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || h.magic != kMagic || h.version != 1 || h.sidChars > 256 || h.dimensions == 0 ||
        h.dimensions > 4096) {
        if (error)
            *error = L"Invalid profile header";
        return false;
    }
    std::wstring fileSid(h.sidChars, L'\0');
    f.read(reinterpret_cast<char*>(fileSid.data()), fileSid.size() * sizeof(wchar_t));
    if (!f || fileSid != sid) {
        if (error)
            *error = L"Profile SID mismatch";
        return false;
    }
    std::vector<float> embedding(h.dimensions);
    f.read(reinterpret_cast<char*>(embedding.data()), embedding.size() * sizeof(float));
    if (!f) {
        if (error)
            *error = L"Truncated profile";
        return false;
    }
    profile = {fileSid, std::move(embedding)};
    return true;
}

std::vector<FaceProfile> LoadAllFaceProfiles() {
    std::vector<FaceProfile> out;
    std::error_code ec;
    if (!std::filesystem::exists(ProfilesDir(), ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(ProfilesDir(), ec)) {
        if (ec)
            break;
        if (!e.is_regular_file() || e.path().extension() != L".fap")
            continue;
        std::wstring sid = e.path().stem().wstring();
        FaceProfile p;
        if (LoadFaceProfile(sid, p, nullptr))
            out.push_back(std::move(p));
    }
    return out;
}

bool DeleteFaceProfile(const std::wstring& sid) {
    std::error_code ec;
    return !std::filesystem::exists(ProfilePath(sid), ec) ||
           std::filesystem::remove(ProfilePath(sid), ec);
}
} // namespace faceauth
