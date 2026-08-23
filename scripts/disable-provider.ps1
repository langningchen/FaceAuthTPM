#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
$ErrorActionPreference='Stop'
$id=[Security.Principal.WindowsIdentity]::GetCurrent();$p=New-Object Security.Principal.WindowsPrincipal($id)
if(-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run PowerShell as Administrator.'}
$dll='C:\Program Files\FaceAuth\FaceAuthCredentialProvider.dll'
if(Test-Path $dll){& "$env:SystemRoot\System32\regsvr32.exe" /s /u $dll}
$guid='{4CF34D82-0D5D-4A5C-9E46-64C16F348C62}'
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$guid"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$guid"
Write-Host 'FaceAuth Credential Provider disabled. Microsoft providers were not modified. A reboot is recommended.' -ForegroundColor Green
