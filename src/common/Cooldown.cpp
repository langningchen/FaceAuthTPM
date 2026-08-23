// SPDX-License-Identifier: GPL-3.0-only
#include "common/Cooldown.h"
#include "common/Paths.h"
#include "common/Security.h"
#include <windows.h>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace faceauth {
static long long NowSeconds(){ return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
void SetCooldown(const std::wstring& sid,unsigned seconds){
    EnsureDataDirectories();
    std::ofstream f(CooldownPath(sid),std::ios::trunc); if(f){ f << (NowSeconds()+seconds); f.close(); ApplyAdminSystemOnlyAcl(CooldownPath(sid)); }
}
bool IsInCooldown(const std::wstring& sid){
    std::ifstream f(CooldownPath(sid)); long long until=0; if(!(f>>until)) return false;
    if(until<=NowSeconds()){ ClearCooldown(sid); return false; } return true;
}
void ClearCooldown(const std::wstring& sid){ std::error_code ec; std::filesystem::remove(CooldownPath(sid),ec); }
}
