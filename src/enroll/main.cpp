// SPDX-License-Identifier: GPL-3.0-only
#include "vision/FaceEngine.h"
#include "common/FaceProfile.h"
#include "common/IdentityRecord.h"
#include "common/TpmVault.h"
#include "common/Security.h"
#include "common/Cooldown.h"
#include "common/Paths.h"
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::wstring ReadPasswordNoEcho(){
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE); DWORD oldMode=0;
    if(!GetConsoleMode(in,&oldMode)){ std::wstring s; std::getline(std::wcin,s); return s; }
    SetConsoleMode(in,oldMode & ~ENABLE_ECHO_INPUT);
    std::wstring s; std::getline(std::wcin,s);
    SetConsoleMode(in,oldMode); std::wcout << L"\n"; return s;
}

void Usage(){
    std::wcout << L"FaceAuthEnroll commands:\n"
                  L"  enroll [--camera N] [--qualified NAME] [--display NAME]\n"
                  L"      Capture face, save TPM-encrypted Windows password, and cache identity metadata.\n"
                  L"  identity [--qualified NAME] [--display NAME]\n"
                  L"      Create/refresh identity metadata only. No camera or password is needed.\n"
                  L"      For a personal Microsoft account, override example:\n"
                  L"        --qualified \"MicrosoftAccount\\person@example.com\"\n"
                  L"  test [--camera N]     Verify face/profile/vault/identity metadata (does not decrypt password).\n"
                  L"  remove                Remove current user's FaceAuth enrollment.\n";
}

std::wstring GetOption(int argc,wchar_t** argv,const wchar_t* option){
    for(int i=2;i+1<argc;++i) if(std::wstring(argv[i])==option) return argv[i+1];
    return {};
}

int GetCamera(int argc,wchar_t** argv){
    for(int i=2;i+1<argc;++i) if(std::wstring(argv[i])==L"--camera") return _wtoi(argv[i+1]);
    return 0;
}

bool PrepareIdentity(int argc,wchar_t** argv,faceauth::IdentityRecord& identity,std::wstring& error){
    const std::wstring qualified=GetOption(argc,argv,L"--qualified");
    const std::wstring display=GetOption(argc,argv,L"--display");
    if(!faceauth::BuildCurrentIdentityRecord(identity,qualified,display,&error))return false;
    return true;
}

void PrintIdentity(const faceauth::IdentityRecord& identity){
    std::wcout << L"Identity SID:       " << identity.sid << L"\n"
               << L"Qualified name:     " << identity.qualifiedName << L"\n"
               << L"Display name:       " << identity.displayName << L"\n"
               << L"Online identity:    " << (identity.onlineIdentity?L"yes":L"no") << L"\n";
    if(!identity.onlineIdentity){
        std::wcout << L"Note: FaceAuth 0.5 production auto-logon currently enumerates online/Microsoft identities only.\n";
    }
}
}

int wmain(int argc,wchar_t** argv){
    if(argc<2){Usage();return 1;}
    if(!faceauth::IsProcessElevated()){
        std::wcerr << L"Run this tool from an elevated (Administrator) terminal.\n";return 2;
    }
    if(!faceauth::EnsureDataDirectories()){
        std::wcerr << L"Could not create C:\\ProgramData\\FaceAuth securely.\n";return 3;
    }
    const std::wstring sid=faceauth::CurrentUserSidString();
    if(sid.empty()){std::wcerr << L"Could not determine current user's SID.\n";return 4;}
    const std::wstring cmd=argv[1];

    if(cmd==L"remove"){
        bool a=faceauth::DeleteCredentialForSid(sid);
        bool b=faceauth::DeleteFaceProfile(sid);
        bool c=faceauth::DeleteIdentityRecord(sid);
        faceauth::ClearCooldown(sid);
        std::wcout << (a&&b&&c?L"Enrollment removed.\n":L"Enrollment removal had an error.\n");
        return a&&b&&c?0:5;
    }

    if(cmd==L"identity"){
        faceauth::IdentityRecord identity;std::wstring error;
        if(!PrepareIdentity(argc,argv,identity,error)){
            std::wcerr << L"Identity detection failed: " << error << L"\n"
                       << L"For a Microsoft account, retry with --qualified \"MicrosoftAccount\\you@example.com\".\n";
            return 14;
        }
        PrintIdentity(identity);
        if(!faceauth::SaveIdentityRecord(identity,&error)){
            std::wcerr << L"Identity save failed: " << error << L"\n";return 15;
        }
        std::wcout << L"Identity metadata saved. No face/password enrollment was changed.\n";
        return 0;
    }

    const int camera=GetCamera(argc,argv);
    if(cmd==L"test"){
        faceauth::IdentityRecord identity;std::wstring error;
        if(!faceauth::LoadIdentityRecord(sid,identity,&error)){
            std::wcerr << L"Identity metadata: " << error << L"\n"
                       << L"Run: FaceAuthEnroll.exe identity\n";
            return 14;
        }
        PrintIdentity(identity);
        faceauth::FaceEngine engine;
        if(!engine.Initialize(&error)){std::wcerr << L"Face engine error: " << error << L"\n";return 6;}
        faceauth::FaceProfile p;
        if(!faceauth::LoadFaceProfile(sid,p,&error)){std::wcerr << L"Profile: " << error << L"\n";return 7;}
        if(!faceauth::CredentialExists(sid)){std::wcerr << L"Encrypted credential blob is missing.\n";return 8;}
        std::wcout << L"Encrypted credential blob is present. The TPM private key is SYSTEM-only by design;\n"
                      L"run scripts\\test-vault-as-system.ps1 to verify decryption without exposing the password.\n"
                      L"Looking for your face...\n";
        std::wstring found=faceauth::ScanForKnownFace(engine,{p},camera,10000);
        std::wcout << (found==sid?L"Face match OK.\n":L"Face did not match within 10 seconds.\n");
        return found==sid?0:9;
    }

    if(cmd!=L"enroll"){Usage();return 1;}

    faceauth::IdentityRecord identity;std::wstring error;
    if(!PrepareIdentity(argc,argv,identity,error)){
        std::wcerr << L"Identity detection failed before enrollment: " << error << L"\n"
                   << L"For a Microsoft account, retry with --qualified \"MicrosoftAccount\\you@example.com\".\n";
        return 14;
    }
    PrintIdentity(identity);
    if(!faceauth::SaveIdentityRecord(identity,&error)){
        std::wcerr << L"Identity save failed: " << error << L"\n";return 15;
    }

    faceauth::FaceEngine engine;
    if(!engine.Initialize(&error)){std::wcerr << L"Face engine error: " << error << L"\n";return 6;}

    std::wcout << L"Enrolling SID: " << sid << L"\n"
                  L"Keep exactly one face in view. The camera window will collect 30 samples.\n";
    std::vector<float> embedding;
    if(!faceauth::CaptureEnrollmentTemplate(engine,embedding,camera,&error)){
        std::wcerr << L"Face capture failed: " << error << L"\n";return 10;
    }

    std::wcout << L"Enter the Windows PASSWORD used by Windows for this account.\n"
                  L"For Microsoft accounts this is the Microsoft account password; do not enter a Windows Hello PIN.\n"
                  L"Input is hidden: ";
    std::wstring password=ReadPasswordNoEcho();
    if(password.empty()){std::wcerr << L"Empty password; enrollment cancelled.\n";return 11;}

    if(!faceauth::StorePasswordForSid(sid,password,&error)){
        SecureZeroMemory(password.data(),password.size()*sizeof(wchar_t));
        std::wcerr << L"TPM vault error: " << error << L"\n";return 12;
    }
    SecureZeroMemory(password.data(),password.size()*sizeof(wchar_t));password.clear();

    faceauth::FaceProfile profile{sid,std::move(embedding)};
    if(!faceauth::SaveFaceProfile(profile,&error)){
        faceauth::DeleteCredentialForSid(sid);
        std::wcerr << L"Profile save error: " << error << L"\n";return 13;
    }
    faceauth::ClearCooldown(sid);
    std::wcout << L"Enrollment complete. Run 'FaceAuthEnroll test', then scripts\\test-vault-as-system.ps1, before enabling the Credential Provider.\n";
    return 0;
}
