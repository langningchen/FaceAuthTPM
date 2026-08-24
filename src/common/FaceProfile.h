// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>

namespace faceauth {
struct FaceProfile {
    std::wstring sid;
    std::vector<float> embedding;
};

bool SaveFaceProfile(const FaceProfile& profile, std::wstring* error = nullptr);
bool LoadFaceProfile(const std::wstring& sid, FaceProfile& profile, std::wstring* error = nullptr);
std::vector<FaceProfile> LoadAllFaceProfiles();
bool DeleteFaceProfile(const std::wstring& sid);
} // namespace faceauth
