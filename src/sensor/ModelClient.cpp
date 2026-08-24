// SPDX-License-Identifier: GPL-3.0-only
#include "sensor/ModelClient.h"
#include "common/Constants.h"
#include "common/ModelServiceProtocol.h"
#include <chrono>
#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

namespace faceauth {
namespace {
bool WriteExact(HANDLE h, const void* buffer, DWORD bytes) {
    const auto* p = static_cast<const unsigned char*>(buffer);
    DWORD done = 0;
    while (done < bytes) {
        DWORD wrote = 0;
        if (!WriteFile(h, p + done, bytes - done, &wrote, nullptr) || wrote == 0)
            return false;
        done += wrote;
    }
    return true;
}

bool ReadExact(HANDLE h, void* buffer, DWORD bytes) {
    auto* p = static_cast<unsigned char*>(buffer);
    DWORD done = 0;
    while (done < bytes) {
        DWORD got = 0;
        if (!ReadFile(h, p + done, bytes - done, &got, nullptr) || got == 0)
            return false;
        done += got;
    }
    return true;
}

HANDLE ConnectModelPipe() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE h = CreateFileW(kModelPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;
        DWORD e = GetLastError();
        if (e != ERROR_PIPE_BUSY && e != ERROR_FILE_NOT_FOUND)
            break;
        WaitNamedPipeW(kModelPipeName, 250);
    }
    return INVALID_HANDLE_VALUE;
}
} // namespace

bool ScanUsingModelService(cv::VideoCapture& camera, int timeoutMs, std::wstring& sid,
                           std::wstring* error, const cv::Mat* firstFrame) {
    sid.clear();
    if (!camera.isOpened()) {
        if (error)
            *error = L"camera is not open";
        return false;
    }

    HANDLE pipe = ConnectModelPipe();
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error)
            *error = L"preloaded model service is not available (Win32 " +
                     std::to_wstring(GetLastError()) + L")";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::wstring lastSid;
    int consecutive = 0;
    bool useFirstFrame = firstFrame && !firstFrame->empty();
    while (std::chrono::steady_clock::now() < deadline) {
        cv::Mat frame;
        if (useFirstFrame) {
            frame = *firstFrame;
            useFirstFrame = false;
        } else if (!camera.read(frame)) {
            Sleep(20);
            continue;
        }
        if (frame.empty())
            continue;
        if (frame.type() != CV_8UC3) {
            if (error)
                *error = L"unexpected camera frame format";
            CloseHandle(pipe);
            return false;
        }
        cv::Mat contiguous = frame.isContinuous() ? frame : frame.clone();
        const std::uint64_t bytes64 = static_cast<std::uint64_t>(contiguous.cols) *
                                      static_cast<std::uint64_t>(contiguous.rows) * 3u;
        if (bytes64 == 0 || bytes64 > kModelMaxFrameBytes) {
            if (error)
                *error = L"camera frame is too large";
            CloseHandle(pipe);
            return false;
        }

        ModelFrameHeader header{kModelPipeMagic,
                                kModelPipeVersion,
                                static_cast<std::uint32_t>(contiguous.cols),
                                static_cast<std::uint32_t>(contiguous.rows),
                                3,
                                static_cast<std::uint32_t>(contiguous.cols * 3),
                                static_cast<std::uint32_t>(bytes64)};
        if (!WriteExact(pipe, &header, sizeof(header)) ||
            !WriteExact(pipe, contiguous.data, header.dataBytes)) {
            if (error)
                *error = L"model service pipe write failed";
            CloseHandle(pipe);
            return false;
        }

        ModelResponseHeader response{};
        if (!ReadExact(pipe, &response, sizeof(response)) || response.magic != kModelPipeMagic ||
            response.version != kModelPipeVersion || response.sidChars > 256) {
            if (error)
                *error = L"model service returned an invalid response";
            CloseHandle(pipe);
            return false;
        }
        std::wstring candidate;
        if (response.sidChars) {
            candidate.resize(response.sidChars);
            const DWORD sidBytes = static_cast<DWORD>(candidate.size() * sizeof(wchar_t));
            if (!ReadExact(pipe, candidate.data(), sidBytes)) {
                if (error)
                    *error = L"model service SID read failed";
                CloseHandle(pipe);
                return false;
            }
        }

        if (response.status == kModelStatusError) {
            if (error)
                *error = L"model service reported an inference error";
            CloseHandle(pipe);
            return false;
        }
        if (response.status == kModelStatusMatch && !candidate.empty()) {
            if (candidate == lastSid)
                ++consecutive;
            else {
                lastSid = std::move(candidate);
                consecutive = 1;
            }
            if (consecutive >= kRequiredConsecutiveMatches) {
                sid = lastSid;
                CloseHandle(pipe);
                return true;
            }
        } else {
            lastSid.clear();
            consecutive = 0;
        }
    }

    CloseHandle(pipe);
    return true;
}
} // namespace faceauth
