// SPDX-License-Identifier: GPL-3.0-only
#include "cp/CpHelpers.h"
#include "common/Paths.h"
#include "common/Constants.h"
#include <windows.h>
#include <objbase.h>
#include <wtsapi32.h>
#include <ntsecapi.h>
#include <vector>
#include <string>
#include <algorithm>

namespace faceauth {
bool IsPhysicalConsoleSession(){
    LPWSTR buffer=nullptr; DWORD bytes=0;
    if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSClientProtocolType,&buffer,&bytes)) return false;
    bool console=false;
    if(buffer && bytes>=sizeof(USHORT)) console=(*reinterpret_cast<USHORT*>(buffer)==0);
    if(buffer) WTSFreeMemory(buffer);
    return console;
}

bool IsFaceAuthSessionAllowed(){
#ifdef FACEAUTH_TEST_ALLOW_REMOTE
    return true;
#else
    return IsPhysicalConsoleSession();
#endif
}

HRESULT RetrieveNegotiateAuthPackage(ULONG* packageId){
    if(!packageId) return E_INVALIDARG;
    *packageId=0; LSA_HANDLE h=nullptr;
    NTSTATUS st=LsaConnectUntrusted(&h); if(st<0) return HRESULT_FROM_WIN32(LsaNtStatusToWinError(st));
    LSA_STRING name{}; const char kName[]="Negotiate"; name.Buffer=const_cast<PCHAR>(kName); name.Length=sizeof(kName)-1; name.MaximumLength=sizeof(kName);
    st=LsaLookupAuthenticationPackage(h,&name,packageId); LsaDeregisterLogonProcess(h);
    return st<0?HRESULT_FROM_WIN32(LsaNtStatusToWinError(st)):S_OK;
}

HRESULT DupString(PCWSTR src,PWSTR* dst){
    if(!dst) return E_INVALIDARG; *dst=nullptr; if(!src) src=L"";
    size_t chars=wcslen(src)+1; auto* p=static_cast<PWSTR>(CoTaskMemAlloc(chars*sizeof(wchar_t))); if(!p) return E_OUTOFMEMORY;
    memcpy(p,src,chars*sizeof(wchar_t)); *dst=p; return S_OK;
}

std::wstring RunFaceSensor(void* moduleHandle,int timeoutMs){
    auto exe=ModuleDirectory(moduleHandle)/L"FaceAuthSensor.exe";
    if(!std::filesystem::exists(exe)) return {};
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0)) return {};
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=writePipe; si.hStdError=writePipe; si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd=L"\""+exe.wstring()+L"\" --timeout-ms "+std::to_wstring(timeoutMs);
    std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end()); mutableCmd.push_back(L'\0');
    BOOL ok=CreateProcessW(exe.c_str(),mutableCmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!ok){ CloseHandle(readPipe); return {}; }

    // Do not wait for the Sensor process to finish tearing down the camera before
    // using a successful match. The child writes the SID before leaving wmain();
    // VideoCapture destruction can take hundreds of milliseconds afterwards.
    // Poll the pipe and return as soon as a complete SID line arrives, while still
    // enforcing the same hard timeout if the child hangs or produces no result.
    const ULONGLONG deadline=GetTickCount64()+static_cast<ULONGLONG>(timeoutMs+kSensorStartupGraceMs);
    std::string out;
    bool timedOut=false;
    for(;;){
        DWORD available=0;
        if(PeekNamedPipe(readPipe,nullptr,0,nullptr,&available,nullptr) && available){
            char buf[256];
            DWORD got=0;
            const DWORD want=std::min<DWORD>(available,sizeof(buf));
            if(ReadFile(readPipe,buf,want,&got,nullptr) && got) out.append(buf,buf+got);
            size_t scan=0;
            while(scan<out.size()){
                const auto pos=out.find_first_of("\r\n",scan);
                if(pos==std::string::npos) break;
                const std::string line=out.substr(scan,pos-scan);
                scan=pos+1;
                if(line.empty()) continue;
                if(line.rfind("S-1-",0)==0){
                    int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),nullptr,0);
                    if(chars>0){
                        std::wstring sid(chars,L'\0');
                        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),sid.data(),chars);
                        CloseHandle(readPipe); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                        return sid;
                    }
                }
            }
        }

        const DWORD state=WaitForSingleObject(pi.hProcess,0);
        if(state==WAIT_OBJECT_0) break;
        if(GetTickCount64()>=deadline){
            timedOut=true;
            TerminateProcess(pi.hProcess,124);
            WaitForSingleObject(pi.hProcess,1000);
            break;
        }
        Sleep(5);
    }

    // Drain anything written just before a normal process exit.
    if(!timedOut){
        for(;;){
            char buf[256]; DWORD got=0;
            if(!ReadFile(readPipe,buf,sizeof(buf),&got,nullptr)||got==0) break;
            out.append(buf,buf+got);
        }
    }
    CloseHandle(readPipe); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    size_t scan=0;
    while(scan<out.size()){
        auto pos=out.find_first_of("\r\n",scan);
        if(pos==std::string::npos) pos=out.size();
        const std::string line=out.substr(scan,pos-scan);
        scan=pos+1;
        if(line.rfind("S-1-",0)!=0) continue;
        int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),nullptr,0);
        if(chars<=0) continue;
        std::wstring sid(chars,L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line.data(),static_cast<int>(line.size()),sid.data(),chars);
        return sid;
    }
    return {};
}
}
