// SPDX-License-Identifier: GPL-3.0-only
#include "common/Security.h"
#include "common/TpmVault.h"
#include <iostream>
#include <string>
#include <windows.h>

namespace {
void Usage() {
    std::wcerr << L"Usage:\n"
                  L"  FaceAuthVaultProbe.exe --bootstrap [--result-file PATH]\n"
                  L"  FaceAuthVaultProbe.exe --sid S-1-... [--result-file PATH]\n";
}

bool WriteResultFile(const std::wstring& path, const std::wstring& text) {
    if (path.empty())
        return true;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    BOOL ok = WriteFile(h, &bom, sizeof(bom), &written, nullptr);
    if (ok && !text.empty()) {
        const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        ok = WriteFile(h, text.data(), bytes, &written, nullptr);
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok == TRUE;
}

int Finish(int code, const std::wstring& message, const std::wstring& resultFile, bool error) {
    if (error)
        std::wcerr << message << L"\n";
    else
        std::wcout << message << L"\n";
    if (!resultFile.empty() && !WriteResultFile(resultFile, message + L"\r\n")) {
        std::wcerr << L"Warning: could not write diagnostic result file: " << resultFile << L"\n";
        if (code == 0)
            return 7;
    }
    return code;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    bool bootstrap = false;
    std::wstring sid;
    std::wstring resultFile;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--bootstrap")
            bootstrap = true;
        else if (arg == L"--sid" && i + 1 < argc)
            sid = argv[++i];
        else if (arg == L"--result-file" && i + 1 < argc)
            resultFile = argv[++i];
    }
    if (!faceauth::IsRunningAsLocalSystem()) {
        return Finish(5,
                      L"This probe must run as LocalSystem. Use the provided PowerShell scripts.",
                      resultFile, true);
    }
    if (bootstrap) {
        std::wstring error;
        if (!faceauth::EnsureTpmVaultKey(&error)) {
            return Finish(6, L"TPM bootstrap failed: " + error, resultFile, true);
        }
        return Finish(0,
                      L"TPM vault bootstrap OK. Private key is machine-bound, non-exportable, and "
                      L"SYSTEM-only.",
                      resultFile, false);
    }
    if (sid.empty()) {
        Usage();
        return Finish(2, L"Missing --sid argument.", resultFile, true);
    }

    faceauth::SecurePassword password;
    std::wstring error;
    if (!faceauth::DecryptPasswordForSid(sid, password, &error)) {
        return Finish(3, L"Vault probe failed: " + error, resultFile, true);
    }
    if (password.empty()) {
        return Finish(4, L"Vault probe failed: decrypted credential is empty.", resultFile, true);
    }

    // Deliberately never print, persist, or return the plaintext credential.
    // SecurePassword wipes its backing buffer during destruction.
    return Finish(0, L"TPM vault decrypt OK for SID " + sid + L". Plaintext was not displayed.",
                  resultFile, false);
}
