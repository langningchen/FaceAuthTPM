// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace faceauth {
inline constexpr wchar_t kModelServiceName[] = L"FaceAuthModel";
inline constexpr wchar_t kModelPipeName[] = L"\\\\.\\pipe\\FaceAuthModel-v1";
inline constexpr std::uint32_t kModelPipeMagic = 0x314D4146u; // "FAM1" little-endian
inline constexpr std::uint32_t kModelPipeVersion = 1;
inline constexpr std::uint32_t kModelStatusNoMatch = 0;
inline constexpr std::uint32_t kModelStatusMatch = 1;
inline constexpr std::uint32_t kModelStatusError = 2;
inline constexpr std::uint32_t kModelMaxFrameBytes = 32u * 1024u * 1024u;

#pragma pack(push, 1)
struct ModelFrameHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;
    std::uint32_t stride;
    std::uint32_t dataBytes;
};

struct ModelResponseHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t status;
    std::uint32_t sidChars; // UTF-16 code units, no terminator.
};
#pragma pack(pop)
}
