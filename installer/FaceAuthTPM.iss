; SPDX-License-Identifier: GPL-3.0-only
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif

#define HasVCRedist FileExists(AddBackslash(SourcePath) + "payload\vc_redist.x64.exe")

[Setup]
AppId={{9E8EBA7F-5B5D-4706-AF0D-C2BC64D6D5A8}
AppName=FaceAuthTPM
AppVersion={#MyAppVersion}
AppPublisher=FaceAuthTPM contributors
DefaultDirName={autopf}\FaceAuth
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=FaceAuthTPM-Setup
LicenseFile=..\LICENSE
UninstallDisplayName=FaceAuthTPM
Uninstallable=yes
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Files]
; The existing hardened PowerShell installer is executed from a temporary
; deployment tree. It owns the fixed Program Files/ProgramData ACL setup,
; TPM bootstrap and model-service creation.
Source: "..\deploy\FaceAuthTPM\*"; DestDir: "{tmp}\FaceAuthTPM"; Flags: ignoreversion recursesubdirs createallsubdirs deleteafterinstall
Source: "install-entry.ps1"; DestDir: "{tmp}\FaceAuthTPM"; Flags: ignoreversion deleteafterinstall

; User-facing command-line configuration tool and notices remain installed.
Source: "..\release\FaceAuthTPM-Configure.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\VERSION.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "COPYING.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\YuNet-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\third_party\SFace-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\third_party\vcpkg-licenses\*"; DestDir: "{app}\licenses\vcpkg"; Flags: ignoreversion recursesubdirs createallsubdirs
#if HasVCRedist
Source: "payload\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
#endif

[Run]
#if HasVCRedist
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: waituntilterminated runhidden
#endif
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -ExecutionPolicy Bypass -File ""{tmp}\FaceAuthTPM\install-entry.ps1"" -PayloadRoot ""{tmp}\FaceAuthTPM"""; StatusMsg: "Installing or updating FaceAuthTPM..."; Flags: waituntilterminated
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -File ""{app}\FaceAuthTPM-Configure.ps1"" help"; Description: "Open FaceAuthTPM command-line setup instructions"; Flags: postinstall skipifsilent nowait

[UninstallRun]
; Preserve ProgramData enrollment by default. The user can explicitly run
; `FaceAuthTPM-Configure.ps1 purge-data` before uninstalling when desired.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -ExecutionPolicy Bypass -File ""{app}\FaceAuthTPM-Configure.ps1"" uninstall-runtime"; Flags: runhidden waituntilterminated; RunOnceId: "FaceAuthTPMRuntimeCleanup"

[UninstallDelete]
; Binaries copied by install.ps1 are not individually tracked by Inno Setup.
Type: filesandordirs; Name: "{app}"
