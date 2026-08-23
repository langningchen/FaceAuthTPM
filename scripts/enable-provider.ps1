#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
param([switch]$AllowVmTestBuild)
$ErrorActionPreference='Stop'
$id=[Security.Principal.WindowsIdentity]::GetCurrent();$p=New-Object Security.Principal.WindowsPrincipal($id)
if(-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run PowerShell as Administrator.'}
$install='C:\Program Files\FaceAuth'
$dll=Join-Path $install 'FaceAuthCredentialProvider.dll'
if(-not(Test-Path $dll)){throw 'FaceAuth is not installed.'}
if((Test-Path (Join-Path $install 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt')) -and -not $AllowVmTestBuild){
    throw 'This installed binary is the RDP-enabled VM test build. In the disposable VM only, rerun enable-provider.ps1 -AllowVmTestBuild.'
}
if(-not(Test-Path 'C:\ProgramData\FaceAuth\vault-public.blob')){throw 'TPM vault public key is missing. Run scripts\bootstrap-vault.ps1 first.'}
$creds=@(Get-ChildItem 'C:\ProgramData\FaceAuth\credentials' -Filter '*.fav' -ErrorAction SilentlyContinue)
$profiles=@(Get-ChildItem 'C:\ProgramData\FaceAuth\profiles' -Filter '*.fap' -ErrorAction SilentlyContinue)
$identities=@(Get-ChildItem 'C:\ProgramData\FaceAuth\identities' -Filter '*.fai' -ErrorAction SilentlyContinue)
if($creds.Count -eq 0 -or $profiles.Count -eq 0){throw 'No complete enrollment found. Enroll and test users before enabling FaceAuth.'}
$identitySids=@{}; foreach($i in $identities){$identitySids[$i.BaseName]=$true}
$credentialSids=@{}; foreach($c in $creds){$credentialSids[$c.BaseName]=$true}
$missing=@($profiles | Where-Object {$credentialSids.ContainsKey($_.BaseName) -and -not $identitySids.ContainsKey($_.BaseName)} | ForEach-Object {$_.BaseName})
if($missing.Count -gt 0){
    Write-Host 'FaceAuth 0.5 intentionally no longer waits for ICredentialProviderSetUserArray.' -ForegroundColor Yellow
    Write-Host 'The following enrolled SIDs need one-time identity metadata:' -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" }
    throw 'Log in as each listed user and run: & "C:\Program Files\FaceAuth\FaceAuthEnroll.exe" identity  (use --qualified "MicrosoftAccount\you@example.com" if auto-detection is not exact). Then rerun enable-provider.ps1.'
}
$modelService=Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
if(-not $modelService){throw 'FaceAuth model preloader service is not installed. Run install.ps1 first.'}
if($modelService.Status -ne 'Running'){Start-Service -Name 'FaceAuthModel'}
& "$env:SystemRoot\System32\regsvr32.exe" /s $dll
if($LASTEXITCODE){throw "regsvr32 failed: $LASTEXITCODE"}
Write-Host 'FaceAuth Credential Provider enabled. Use Win+L for the first test; a reboot is not required.' -ForegroundColor Green
Write-Host 'Microsoft Password and Windows Hello providers were not modified.'
