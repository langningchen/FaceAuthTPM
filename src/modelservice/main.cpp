// SPDX-License-Identifier: GPL-3.0-only
#include "common/Constants.h"
#include "common/FaceProfile.h"
#include "common/ModelServiceProtocol.h"
#include "common/Paths.h"
#include "vision/FaceEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <sddl.h>
#include <string>
#include <vector>
#include <windows.h>

namespace {
using Clock = std::chrono::steady_clock;
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;

std::string Utf8(const std::wstring& s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

void AppendLog(const wchar_t* stage, const std::wstring& detail = {}) {
    const auto root = faceauth::ProgramDataRoot();
    if (root.empty())
        return;
    const auto path = root / L"model-service.log";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME st{};
    GetSystemTime(&st);
    std::wstring line = L"[" + std::to_wstring(st.wYear) + L"-" + (st.wMonth < 10 ? L"0" : L"") +
                        std::to_wstring(st.wMonth) + L"-" + (st.wDay < 10 ? L"0" : L"") +
                        std::to_wstring(st.wDay) + L"T" + (st.wHour < 10 ? L"0" : L"") +
                        std::to_wstring(st.wHour) + L":" + (st.wMinute < 10 ? L"0" : L"") +
                        std::to_wstring(st.wMinute) + L":" + (st.wSecond < 10 ? L"0" : L"") +
                        std::to_wstring(st.wSecond) + L"Z] " + stage;
    if (!detail.empty())
        line += L" | " + detail;
    line += L"\r\n";
    const auto bytes = Utf8(line);
    DWORD written = 0;
    WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

void SetServiceState(DWORD state, DWORD win32Exit = NO_ERROR, DWORD waitHint = 0) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted =
        (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    g_status.dwWin32ExitCode = win32Exit;
    g_status.dwWaitHint = waitHint;
    if (g_statusHandle)
        SetServiceStatus(g_statusHandle, &g_status);
}

DWORD WINAPI HandlerEx(DWORD control, DWORD, void*, void*) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (g_stopEvent)
            SetEvent(g_stopEvent);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
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

bool SendResponse(HANDLE pipe, std::uint32_t status, const std::wstring& sid = {}) {
    faceauth::ModelResponseHeader response{faceauth::kModelPipeMagic, faceauth::kModelPipeVersion,
                                           status, static_cast<std::uint32_t>(sid.size())};
    if (!WriteExact(pipe, &response, sizeof(response)))
        return false;
    if (!sid.empty()) {
        const DWORD bytes = static_cast<DWORD>(sid.size() * sizeof(wchar_t));
        if (!WriteExact(pipe, sid.data(), bytes))
            return false;
    }
    return true;
}

std::wstring MatchFrame(faceauth::FaceEngine& engine,
                        const std::vector<faceauth::FaceProfile>& profiles, const cv::Mat& frame) {
    // Keep camera startup on its native/default media type. If the device gives
    // us a large frame (for example 4K), downscale in the already-warm model
    // service instead of renegotiating the capture device in LogonUI.
    cv::Mat inference = frame;
    if (frame.cols > 1280 || frame.rows > 720) {
        const double scale = std::min(1280.0 / static_cast<double>(frame.cols),
                                      720.0 / static_cast<double>(frame.rows));
        cv::resize(frame, inference, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    std::vector<float> embedding;
    if (!engine.ExtractEmbedding(inference, embedding, nullptr))
        return {};
    double best = -2.0, second = -2.0;
    const faceauth::FaceProfile* winner = nullptr;
    for (const auto& p : profiles) {
        const double score = faceauth::FaceEngine::Cosine(embedding, p.embedding);
        if (score > best) {
            second = best;
            best = score;
            winner = &p;
        } else if (score > second)
            second = score;
    }
    if (!winner || best < faceauth::kMatchThreshold)
        return {};
    if (profiles.size() > 1 && best - second < faceauth::kMatchMargin)
        return {};
    return winner->sid;
}

void HandleClient(HANDLE pipe, faceauth::FaceEngine& engine) {
    auto profiles = faceauth::LoadAllFaceProfiles();
    if (profiles.empty()) {
        SendResponse(pipe, faceauth::kModelStatusError);
        return;
    }

    for (;;) {
        faceauth::ModelFrameHeader header{};
        if (!ReadExact(pipe, &header, sizeof(header)))
            return;
        if (header.magic != faceauth::kModelPipeMagic ||
            header.version != faceauth::kModelPipeVersion || header.channels != 3 ||
            header.width == 0 || header.height == 0 ||
            header.stride != header.width * header.channels ||
            header.dataBytes != header.stride * header.height || header.width > 3840 ||
            header.height > 2160 || header.dataBytes > faceauth::kModelMaxFrameBytes) {
            SendResponse(pipe, faceauth::kModelStatusError);
            return;
        }

        std::vector<unsigned char> frameBytes(header.dataBytes);
        if (!ReadExact(pipe, frameBytes.data(), header.dataBytes))
            return;
        cv::Mat frame(static_cast<int>(header.height), static_cast<int>(header.width), CV_8UC3,
                      frameBytes.data(), static_cast<size_t>(header.stride));
        const auto sid = MatchFrame(engine, profiles, frame);
        if (!SendResponse(pipe,
                          sid.empty() ? faceauth::kModelStatusNoMatch : faceauth::kModelStatusMatch,
                          sid))
            return;
        if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0)
            return;
    }
}

bool RunServer(faceauth::FaceEngine& engine) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GRGW;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) {
        AppendLog(L"pipe_security_failed", std::to_wstring(GetLastError()));
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), descriptor, FALSE};

    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(
            faceauth::kModelPipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            AppendLog(L"create_pipe_failed", std::to_wstring(GetLastError()));
            LocalFree(descriptor);
            return false;
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            CloseHandle(pipe);
            LocalFree(descriptor);
            return false;
        }

        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && error == ERROR_PIPE_CONNECTED)
            SetEvent(ov.hEvent);
        else if (!connected && error != ERROR_IO_PENDING) {
            CloseHandle(ov.hEvent);
            CloseHandle(pipe);
            continue;
        }

        HANDLE waits[2] = {g_stopEvent, ov.hEvent};
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            CancelIoEx(pipe, &ov);
            DisconnectNamedPipe(pipe);
            CloseHandle(ov.hEvent);
            CloseHandle(pipe);
            break;
        }

        HandleClient(pipe, engine);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
    }

    LocalFree(descriptor);
    return true;
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(faceauth::kModelServiceName, HandlerEx, nullptr);
    if (!g_statusHandle)
        return;
    SetServiceState(SERVICE_START_PENDING, NO_ERROR, 20000);
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        SetServiceState(SERVICE_STOPPED, GetLastError());
        return;
    }

    const auto started = Clock::now();
    AppendLog(L"service_start");
    faceauth::FaceEngine engine;
    std::wstring error;
    if (!engine.Initialize(&error)) {
        AppendLog(L"models_failed", error);
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        SetServiceState(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }
    std::wstring warmupError;
    if (!engine.WarmUp(&warmupError)) {
        // Warm-up is an optimization, not an authentication requirement. Keep
        // the service available even if a particular OpenCV backend dislikes
        // the synthetic warm-up image; real frames can still succeed.
        AppendLog(L"warmup_warning", warmupError);
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    AppendLog(L"models_ready_and_warmed", L"load_ms=" + std::to_wstring(ms));
    SetServiceState(SERVICE_RUNNING);
    RunServer(engine);
    AppendLog(L"service_stop");
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    SetServiceState(SERVICE_STOPPED);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && _wcsicmp(argv[1], L"--console") == 0) {
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        faceauth::FaceEngine engine;
        std::wstring error;
        if (!engine.Initialize(&error)) {
            fwprintf(stderr, L"FaceAuth model service: %ls\n", error.c_str());
            return 2;
        }
        std::wstring warmupError;
        engine.WarmUp(&warmupError);
        fwprintf(stdout, L"Models loaded. Press Ctrl+C to exit.\n");
        RunServer(engine);
        return 0;
    }
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(faceauth::kModelServiceName), ServiceMain},
                                    {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table))
        return static_cast<int>(GetLastError());
    return 0;
}
