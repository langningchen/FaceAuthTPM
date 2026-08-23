// SPDX-License-Identifier: GPL-3.0-only
#include "common/Paths.h"
#include <windows.h>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Backend {
    const wchar_t* token;
    const wchar_t* label;
    int api;
};

constexpr Backend kBackends[] = {
    {L"msmf",  L"Media Foundation", cv::CAP_MSMF},
    {L"dshow", L"DirectShow",       cv::CAP_DSHOW},
    {L"any",   L"OpenCV auto",      cv::CAP_ANY},
};

struct FormatRequest {
    int width = 0;
    int height = 0;
    int fps = 0;
    bool forced() const { return width > 0 && height > 0; }
};

struct Result {
    std::wstring token;
    std::wstring label;
    bool opened = false;
    bool firstFrame = false;
    long long openMs = -1;
    long long firstFrameMs = -1;
    long long totalMs = -1;
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

const Backend* FindBackend(const std::wstring& token) {
    const auto lower = Lower(token);
    for (const auto& b : kBackends) if (lower == b.token) return &b;
    return nullptr;
}

bool WriteAsciiFile(const std::filesystem::path& path, const std::string& text, std::wstring* error) {
    const auto root = faceauth::ProgramDataRoot();
    if (root.empty()) {
        if (error) *error = L"Could not resolve ProgramData.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) *error = L"Could not write " + path.wstring() + L" (Win32 " + std::to_wstring(GetLastError()) + L"). Run from an elevated PowerShell.";
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != text.size()) {
        if (error) *error = L"Writing camera preference failed.";
        return false;
    }
    return true;
}

bool SaveBackendPreference(const std::wstring& token, std::wstring* error) {
    std::string ascii;
    for (wchar_t c : token) ascii.push_back(static_cast<char>(c));
    ascii += "\r\n";
    return WriteAsciiFile(faceauth::ProgramDataRoot() / L"camera-backend.txt", ascii, error);
}

bool SaveFormatPreference(const FormatRequest& format, std::wstring* error) {
    if (!format.forced()) {
        return WriteAsciiFile(faceauth::ProgramDataRoot() / L"camera-format.txt", "0 0 0\r\n", error);
    }
    const std::string ascii = std::to_string(format.width) + " " + std::to_string(format.height) + " " + std::to_string(format.fps) + "\r\n";
    return WriteAsciiFile(faceauth::ProgramDataRoot() / L"camera-format.txt", ascii, error);
}

Result Measure(const Backend& backend, int cameraIndex, const FormatRequest& format) {
    Result r;
    r.token = backend.token;
    r.label = backend.label;
    cv::VideoCapture camera;
    const auto start = Clock::now();
    if (format.forced()) {
        std::vector<int> params{
            cv::CAP_PROP_FRAME_WIDTH, format.width,
            cv::CAP_PROP_FRAME_HEIGHT, format.height,
        };
        if (format.fps > 0) {
            params.push_back(cv::CAP_PROP_FPS);
            params.push_back(format.fps);
        }
        r.opened = camera.open(cameraIndex, backend.api, params);
    } else {
        r.opened = camera.open(cameraIndex, backend.api);
    }
    const auto afterOpen = Clock::now();
    r.openMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterOpen - start).count();
    if (!r.opened) {
        r.totalMs = r.openMs;
        return r;
    }

    cv::Mat frame;
    r.firstFrame = camera.read(frame) && !frame.empty();
    const auto afterFrame = Clock::now();
    r.firstFrameMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterFrame - afterOpen).count();
    r.totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterFrame - start).count();
    if (r.firstFrame) {
        r.width = frame.cols;
        r.height = frame.rows;
    } else {
        r.width = static_cast<int>(camera.get(cv::CAP_PROP_FRAME_WIDTH));
        r.height = static_cast<int>(camera.get(cv::CAP_PROP_FRAME_HEIGHT));
    }
    r.fps = camera.get(cv::CAP_PROP_FPS);
    camera.release();
    return r;
}

long long Median(std::vector<long long> values) {
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return (n % 2) ? values[n/2] : (values[n/2-1] + values[n/2]) / 2;
}
}

int wmain(int argc, wchar_t** argv) {
    int cameraIndex = 0;
    int repeat = 1;
    int cooldownMs = 500;
    bool saveBest = false;
    bool saveFormat = false;
    std::wstring requested = L"all";
    FormatRequest format{};

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--camera" && i + 1 < argc) cameraIndex = _wtoi(argv[++i]);
        else if (arg == L"--repeat" && i + 1 < argc) repeat = std::max(1, _wtoi(argv[++i]));
        else if (arg == L"--backend" && i + 1 < argc) requested = Lower(argv[++i]);
        else if (arg == L"--width" && i + 1 < argc) format.width = std::max(0, _wtoi(argv[++i]));
        else if (arg == L"--height" && i + 1 < argc) format.height = std::max(0, _wtoi(argv[++i]));
        else if (arg == L"--fps" && i + 1 < argc) format.fps = std::max(0, _wtoi(argv[++i]));
        else if (arg == L"--cooldown-ms" && i + 1 < argc) cooldownMs = std::max(0, _wtoi(argv[++i]));
        else if (arg == L"--save-best") saveBest = true;
        else if (arg == L"--save-format") saveFormat = true;
        else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            std::wprintf(L"FaceAuthCameraProbe [--camera N] [--repeat N] [--backend all|msmf|dshow|any]\n"
                         L"                    [--width W --height H [--fps N]] [--cooldown-ms N]\n"
                         L"                    [--save-best] [--save-format]\n");
            return 0;
        }
    }

    if ((format.width > 0) != (format.height > 0)) {
        std::fwprintf(stderr, L"Specify both --width and --height, or neither.\n");
        return 2;
    }

    std::vector<const Backend*> selected;
    if (requested == L"all") {
        for (const auto& b : kBackends) selected.push_back(&b);
    } else if (const auto* b = FindBackend(requested)) {
        selected.push_back(b);
    } else {
        std::fwprintf(stderr, L"Unknown backend '%ls'. Use all, msmf, dshow, or any.\n", requested.c_str());
        return 2;
    }

    std::wprintf(L"FaceAuth camera startup benchmark\nCamera index: %d   Repeats: %d   Cooldown: %d ms\n", cameraIndex, repeat, cooldownMs);
    if (format.forced()) {
        std::wprintf(L"Requested media type: %dx%d", format.width, format.height);
        if (format.fps > 0) std::wprintf(L" @ %d fps", format.fps);
        std::wprintf(L" (passed atomically to VideoCapture::open)\n\n");
    } else {
        std::wprintf(L"No resolution is forced; this measures native/default startup.\n\n");
    }

    struct Aggregate { const Backend* backend; std::vector<long long> totals; };
    std::vector<Aggregate> aggregates;
    for (const auto* b : selected) aggregates.push_back({b, {}});

    for (int round = 1; round <= repeat; ++round) {
        for (auto& agg : aggregates) {
            const auto r = Measure(*agg.backend, cameraIndex, format);
            std::wprintf(L"[%ls #%d] open=%lld ms", r.token.c_str(), round, r.openMs);
            if (!r.opened) {
                std::wprintf(L"  FAILED\n");
            } else {
                std::wprintf(L"  first-frame=%lld ms  total=%lld ms  %dx%d  fps=%.2f%ls\n",
                             r.firstFrameMs, r.totalMs, r.width, r.height, r.fps,
                             r.firstFrame ? L"" : L"  (no frame)");
                if (r.firstFrame) agg.totals.push_back(r.totalMs);
            }
            if (cooldownMs > 0) Sleep(static_cast<DWORD>(cooldownMs));
        }
    }

    const Backend* best = nullptr;
    long long bestMedian = LLONG_MAX;
    std::wprintf(L"\nSummary (median open+first-frame):\n");
    for (const auto& agg : aggregates) {
        const auto med = Median(agg.totals);
        if (med < 0) {
            std::wprintf(L"  %-5ls : unavailable\n", agg.backend->token);
            continue;
        }
        std::wprintf(L"  %-5ls : %lld ms\n", agg.backend->token, med);
        if (med < bestMedian) { bestMedian = med; best = agg.backend; }
    }

    if (!best) {
        std::fwprintf(stderr, L"No backend produced a frame.\n");
        return 3;
    }
    std::wprintf(L"Best backend: %ls (%ls), median %lld ms\n", best->token, best->label, bestMedian);

    if (saveBest) {
        std::wstring error;
        if (!SaveBackendPreference(best->token, &error)) {
            std::fwprintf(stderr, L"Could not save backend preference: %ls\n", error.c_str());
            return 4;
        }
        std::wprintf(L"Saved backend: %ls -> %ls\n", (faceauth::ProgramDataRoot()/L"camera-backend.txt").c_str(), best->token);
    }
    if (saveFormat) {
        std::wstring error;
        if (!SaveFormatPreference(format, &error)) {
            std::fwprintf(stderr, L"Could not save format preference: %ls\n", error.c_str());
            return 5;
        }
        const auto path = faceauth::ProgramDataRoot() / L"camera-format.txt";
        if (format.forced()) std::wprintf(L"Saved format: %ls -> %dx%d @ %d\n", path.c_str(), format.width, format.height, format.fps);
        else std::wprintf(L"Saved format: %ls -> native/default\n", path.c_str());
    }
    return 0;
}
