// SPDX-License-Identifier: GPL-3.0-only
#include "common/Constants.h"
#include "common/Paths.h"
#include "sensor/ModelClient.h"
#include <windows.h>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Backend {
    const wchar_t* token;
    int api;
};

constexpr Backend kBackends[] = {
    {L"msmf", cv::CAP_MSMF},
    {L"dshow", cv::CAP_DSHOW},
    {L"any", cv::CAP_ANY},
};

std::string Utf8(const std::wstring& s){
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(n),'\0');
    if(n>0) WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}

std::wstring SingleLine(std::wstring s){
    for(wchar_t& c:s) if(c==L'\r'||c==L'\n') c=L' ';
    return s;
}

void AppendStartupLog(const Clock::time_point& start,const wchar_t* stage,const std::wstring& detail={}){
    const auto root=faceauth::ProgramDataRoot();
    if(root.empty()) return;
    const auto path=root/L"sensor-startup.log";

    HANDLE h=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                         nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size{};
    if(GetFileSizeEx(h,&size) && size.QuadPart>256*1024){
        CloseHandle(h);
        h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                      nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE) return;
    }

    SYSTEMTIME st{}; GetSystemTime(&st);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
    std::wstring line=L"["+std::to_wstring(st.wYear)+L"-"+
        (st.wMonth<10?L"0":L"")+std::to_wstring(st.wMonth)+L"-"+
        (st.wDay<10?L"0":L"")+std::to_wstring(st.wDay)+L"T"+
        (st.wHour<10?L"0":L"")+std::to_wstring(st.wHour)+L":"+
        (st.wMinute<10?L"0":L"")+std::to_wstring(st.wMinute)+L":"+
        (st.wSecond<10?L"0":L"")+std::to_wstring(st.wSecond)+L"Z] pid="+
        std::to_wstring(GetCurrentProcessId())+L" +"+std::to_wstring(elapsed)+L"ms "+stage;
    if(!detail.empty()) line+=L" | "+SingleLine(detail);
    line+=L"\r\n";
    const auto utf8=Utf8(line);
    DWORD written=0;
    WriteFile(h,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr);
    CloseHandle(h);
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

struct CameraFormatPreference {
    int width = 0;
    int height = 0;
    int fps = 0;
};

CameraFormatPreference ReadPreferredFormat() {
    CameraFormatPreference pref{};
    const auto path = faceauth::ProgramDataRoot() / L"camera-format.txt";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return pref;
    char buffer[128]{};
    DWORD got = 0;
    ReadFile(h, buffer, sizeof(buffer)-1, &got, nullptr);
    CloseHandle(h);
    std::string text(buffer, buffer + got);
    int w=0,hv=0,f=0;
    if (sscanf_s(text.c_str(), "%d %d %d", &w, &hv, &f) >= 2 && w > 0 && hv > 0) {
        pref.width = w;
        pref.height = hv;
        pref.fps = std::max(0, f);
    }
    return pref;
}

std::wstring ReadPreferredBackend() {
    const auto path = faceauth::ProgramDataRoot() / L"camera-backend.txt";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"msmf";
    char buffer[64]{};
    DWORD got = 0;
    ReadFile(h, buffer, sizeof(buffer)-1, &got, nullptr);
    CloseHandle(h);
    std::wstring value;
    for (DWORD i=0;i<got;++i) {
        const unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c=='\r'||c=='\n'||c==' '||c=='\t') break;
        if (c < 0x80) value.push_back(static_cast<wchar_t>(c));
    }
    value = Lower(value);
    if (value != L"msmf" && value != L"dshow" && value != L"any") return L"msmf";
    return value;
}

std::vector<Backend> BackendOrder(const std::wstring& preferred) {
    std::vector<Backend> out;
    auto add=[&](const Backend& b){
        for(const auto& x:out) if(x.api==b.api) return;
        out.push_back(b);
    };
    for(const auto& b:kBackends) if(preferred==b.token) add(b);
    // Prefer modern Media Foundation as the default fallback. DirectShow remains
    // available for cameras whose driver is materially faster there.
    for(const auto& b:kBackends) add(b);
    return out;
}

bool OpenCameraFast(cv::VideoCapture& camera,int cameraIndex,const Clock::time_point& started,
                    std::wstring& selectedBackend,std::wstring* error){
    const auto preferred=ReadPreferredBackend();
    const auto format=ReadPreferredFormat();
    AppendStartupLog(started,L"camera_backend_preference",preferred);
    if(format.width>0 && format.height>0){
        AppendStartupLog(started,L"camera_format_preference",
            std::to_wstring(format.width)+L"x"+std::to_wstring(format.height)+
            (format.fps>0?L"@"+std::to_wstring(format.fps):L""));
    }else{
        AppendStartupLog(started,L"camera_format_preference",L"native-default");
    }
    for(const auto& backend:BackendOrder(preferred)){
        const auto begin=Clock::now();
        AppendStartupLog(started,L"camera_open_begin",L"backend="+std::wstring(backend.token));
        bool ok=false;
        if(format.width>0 && format.height>0){
            std::vector<int> params{
                cv::CAP_PROP_FRAME_WIDTH, format.width,
                cv::CAP_PROP_FRAME_HEIGHT, format.height,
            };
            if(format.fps>0){
                params.push_back(cv::CAP_PROP_FPS);
                params.push_back(format.fps);
            }
            ok=camera.open(cameraIndex,backend.api,params);
            if(!ok){
                AppendStartupLog(started,L"camera_open_forced_format_failed",
                    L"backend="+std::wstring(backend.token));
            }
        }
        if(!ok) ok=camera.open(cameraIndex,backend.api);
        const auto openMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
        if(ok){
            selectedBackend=backend.token;
            const int w=static_cast<int>(camera.get(cv::CAP_PROP_FRAME_WIDTH));
            const int h=static_cast<int>(camera.get(cv::CAP_PROP_FRAME_HEIGHT));
            const double fps=camera.get(cv::CAP_PROP_FPS);
            AppendStartupLog(started,L"camera_open_success",
                L"backend="+selectedBackend+L" open_ms="+std::to_wstring(openMs)+
                L" format="+std::to_wstring(w)+L"x"+std::to_wstring(h)+
                L" fps="+std::to_wstring(fps));
            return true;
        }
        AppendStartupLog(started,L"camera_open_failed_backend",
            L"backend="+std::wstring(backend.token)+L" open_ms="+std::to_wstring(openMs));
    }
    if(error)*error=L"Could not open USB camera index "+std::to_wstring(cameraIndex);
    return false;
}
}

int wmain(int argc,wchar_t** argv){
    const auto started=Clock::now();
    int timeout=faceauth::kSensorTimeoutMs;
    int cameraIndex=0;
    for(int i=1;i<argc;++i){
        if(std::wstring(argv[i])==L"--timeout-ms" && i+1<argc) timeout=_wtoi(argv[++i]);
        else if(std::wstring(argv[i])==L"--camera" && i+1<argc) cameraIndex=_wtoi(argv[++i]);
    }

    AppendStartupLog(started,L"sensor_start");

    // Do not call set(width/height) after opening the device. If the operator
    // selected a known-fast media type with camera-format.txt, pass it atomically
    // to VideoCapture::open so MSMF negotiates it once. Otherwise use the device's
    // native/default path. This matters on some UVC cameras where LogonUI's default
    // 640x480 type is substantially slower to start than the camera's native mode.
    cv::VideoCapture camera;
    std::wstring cameraError;
    std::wstring selectedBackend;
    if(!OpenCameraFast(camera,cameraIndex,started,selectedBackend,&cameraError)){
        AppendStartupLog(started,L"camera_open_failed",cameraError);
        return 4;
    }
    AppendStartupLog(started,L"camera_opened",L"backend="+selectedBackend);

    // Capture one real frame and reuse it for the first inference request. This
    // separates backend-open time from first-frame time in the log without
    // throwing away the first useful image.
    cv::Mat firstFrame;
    const auto firstBegin=Clock::now();
    bool gotFirst=false;
    while(Clock::now()-firstBegin < std::chrono::milliseconds(1500)){
        if(camera.read(firstFrame) && !firstFrame.empty()){gotFirst=true;break;}
        Sleep(10);
    }
    const auto firstMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-firstBegin).count();
    AppendStartupLog(started,gotFirst?L"camera_first_frame":L"camera_first_frame_timeout",
                     L"backend="+selectedBackend+L" first_frame_ms="+std::to_wstring(firstMs)+
                     (gotFirst?L" format="+std::to_wstring(firstFrame.cols)+L"x"+std::to_wstring(firstFrame.rows):L""));

    std::wstring sid;
    std::wstring serviceError;
    const bool serviceHandled = faceauth::ScanUsingModelService(camera,timeout,sid,&serviceError,
                                                                 gotFirst ? &firstFrame : nullptr);
    if(serviceHandled){
        AppendStartupLog(started,L"preloaded_service_used");
        if(sid.empty()){
            AppendStartupLog(started,L"scan_timeout");
            return 3;
        }
    }else{
        AppendStartupLog(started,L"preloaded_service_unavailable",serviceError);
        return 6;
    }
    AppendStartupLog(started,L"face_matched");

    std::string line=Utf8(sid)+"\n";
    DWORD written=0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),line.data(),static_cast<DWORD>(line.size()),&written,nullptr);
    return 0;
}
