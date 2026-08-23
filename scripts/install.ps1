# SPDX-License-Identifier: GPL-3.0-only
param([string]$BuildDir,[switch]$AllowVmTestBuild)
$ErrorActionPreference='Stop'

function Require-Admin {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=New-Object Security.Principal.WindowsPrincipal($id)
    if(-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run PowerShell as Administrator.'}
}

function Invoke-IcaclsChecked {
    param([Parameter(Mandatory=$true)][string[]]$Arguments,[string]$What='ACL update')
    & icacls.exe @Arguments | Out-Null
    if($LASTEXITCODE -ne 0){ throw "$What failed (icacls exit code $LASTEXITCODE)." }
}

function Repair-FaceAuthDataAcl {
    param([Parameter(Mandatory=$true)][string]$Root)

    if(-not (Test-Path -LiteralPath $Root)){ return }

    # Old builds could create protected child-file DACLs with no usable ACEs.
    # Taking ownership as the built-in Administrators group is safe for this
    # data tree: Administrators are intentionally granted Full Control here,
    # while the TPM private key itself remains SYSTEM-only in the TPM KSP.
    & takeown.exe /F $Root /A /R /D Y | Out-Null
    if($LASTEXITCODE -ne 0){ throw "Could not take ownership of $Root (takeown exit code $LASTEXITCODE)." }

    # Root/directory ACEs inherit to future children.
    Invoke-IcaclsChecked -What "Securing $Root" -Arguments @(
        $Root,
        '/inheritance:r',
        '/grant:r',
        '*S-1-5-18:(OI)(CI)F',
        '*S-1-5-32-544:(OI)(CI)F'
    )

    # Existing directories receive inheritable ACEs.
    Get-ChildItem -LiteralPath $Root -Directory -Recurse -Force -ErrorAction Stop | ForEach-Object {
        Invoke-IcaclsChecked -What "Securing directory $($_.FullName)" -Arguments @(
            $_.FullName,
            '/inheritance:r',
            '/grant:r',
            '*S-1-5-18:(OI)(CI)F',
            '*S-1-5-32-544:(OI)(CI)F'
        )
    }

    # Existing files MUST receive explicit ACEs without OI/CI. 0.2.7 used the
    # directory form for files too, which could leave a protected empty DACL.
    Get-ChildItem -LiteralPath $Root -File -Recurse -Force -ErrorAction Stop | ForEach-Object {
        Invoke-IcaclsChecked -What "Securing file $($_.FullName)" -Arguments @(
            $_.FullName,
            '/inheritance:r',
            '/grant:r',
            '*S-1-5-18:F',
            '*S-1-5-32-544:F'
        )
    }
}

Require-Admin
$root=Split-Path -Parent $PSScriptRoot
if(-not $BuildDir){$BuildDir=Join-Path $root 'out\Release'}
if((Test-Path (Join-Path $BuildDir 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt')) -and -not $AllowVmTestBuild){
    throw 'Refusing to install a VM test build that allows RDP. In the Hyper-V test VM only, rerun with -AllowVmTestBuild.'
}
$install='C:\Program Files\FaceAuth'
$data='C:\ProgramData\FaceAuth'
foreach($required in @('FaceAuthCredentialProvider.dll','FaceAuthSensor.exe','FaceAuthCameraProbe.exe','FaceAuthModelService.exe','FaceAuthEnroll.exe','FaceAuthVaultProbe.exe')){
    if(-not (Test-Path (Join-Path $BuildDir $required))){throw "Build output missing: $required in $BuildDir"}
}
foreach($m in @('face_detection_yunet_2023mar.onnx','face_recognition_sface_2021dec.onnx')){
    if(-not(Test-Path(Join-Path $root "models\$m"))){throw "Missing model $m; run scripts\fetch-models.ps1"}
}

New-Item -ItemType Directory -Force $install | Out-Null
New-Item -ItemType Directory -Force $data | Out-Null

$serviceName='FaceAuthModel'
$existingService=Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if($existingService -and $existingService.Status -ne 'Stopped'){
    Write-Host 'Stopping FaceAuth model preloader for update...' -ForegroundColor Cyan
    Stop-Service -Name $serviceName -Force -ErrorAction Stop
    $existingService.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(10))
}

# Repair any ACL damage left by 0.2.6/0.2.7 before attempting an overwrite.
Repair-FaceAuthDataAcl -Root $data

New-Item -ItemType Directory -Force "$data\models","$data\credentials","$data\profiles","$data\identities","$data\cooldown" | Out-Null
# Ensure newly created directories have the intended inheritable ACL before copy.
Repair-FaceAuthDataAcl -Root $data

Copy-Item -Force (Join-Path $BuildDir 'FaceAuthCredentialProvider.dll') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthSensor.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthCameraProbe.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthModelService.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthEnroll.exe') $install
Copy-Item -Force (Join-Path $BuildDir 'FaceAuthVaultProbe.exe') $install
$marker=Join-Path $BuildDir 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt'
$installedMarker=Join-Path $install 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt'
if(Test-Path $marker){Copy-Item -Force $marker $installedMarker}else{Remove-Item -Force -ErrorAction SilentlyContinue $installedMarker}
Get-ChildItem $BuildDir -Filter '*.dll' | Where-Object {$_.Name -ne 'FaceAuthCredentialProvider.dll'} | Copy-Item -Force -Destination $install
Copy-Item -Force (Join-Path $root 'models\*.onnx') "$data\models"

# Apply final ACLs using different ACE forms for directories vs files.
Repair-FaceAuthDataAcl -Root $data

# Program Files tree: apply inheritable ACEs to directories and explicit ACEs
# to existing files, for the same reason as the ProgramData repair above.
& takeown.exe /F $install /A /R /D Y | Out-Null
if($LASTEXITCODE -ne 0){ throw "Could not take ownership of $install (takeown exit code $LASTEXITCODE)." }
Invoke-IcaclsChecked -What "Securing $install" -Arguments @(
    $install,
    '/inheritance:r',
    '/grant:r',
    '*S-1-5-18:(OI)(CI)RX',
    '*S-1-5-32-544:(OI)(CI)F'
)
Get-ChildItem -LiteralPath $install -Directory -Recurse -Force -ErrorAction Stop | ForEach-Object {
    Invoke-IcaclsChecked -What "Securing install directory $($_.FullName)" -Arguments @(
        $_.FullName,
        '/inheritance:r',
        '/grant:r',
        '*S-1-5-18:(OI)(CI)RX',
        '*S-1-5-32-544:(OI)(CI)F'
    )
}
Get-ChildItem -LiteralPath $install -File -Recurse -Force -ErrorAction Stop | ForEach-Object {
    Invoke-IcaclsChecked -What "Securing installed file $($_.FullName)" -Arguments @(
        $_.FullName,
        '/inheritance:r',
        '/grant:r',
        '*S-1-5-18:RX',
        '*S-1-5-32-544:F'
    )
}

& (Join-Path $PSScriptRoot 'bootstrap-vault.ps1')

$serviceExe=Join-Path $install 'FaceAuthModelService.exe'
$existingService=Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if(-not $existingService){
    Write-Host 'Installing FaceAuth model preloader service...' -ForegroundColor Cyan
    New-Service -Name $serviceName -BinaryPathName ('"'+$serviceExe+'"') -DisplayName 'FaceAuth Model Preloader' -Description 'Preloads FaceAuth YuNet/SFace models into memory. Does not handle passwords or TPM credentials.' -StartupType Automatic | Out-Null
}else{
    Set-Service -Name $serviceName -StartupType Automatic
}
# The service reports RUNNING only after both ONNX models are loaded, so a
# successful Start-Service also verifies that preloading is ready.
Start-Service -Name $serviceName
(Get-Service -Name $serviceName).WaitForStatus('Running',[TimeSpan]::FromSeconds(20))
Write-Host 'FaceAuth model preloader is running; YuNet/SFace are resident in memory.' -ForegroundColor Green

Write-Host 'Files installed and TPM vault bootstrapped. The Credential Provider is NOT enabled yet.' -ForegroundColor Green
Write-Host 'FaceAuth 0.5 no longer waits for Windows SetUserArray.' -ForegroundColor Cyan
Write-Host 'For each EXISTING 0.4.x Microsoft-account enrollment, log in as that user and run:'
Write-Host '  & "C:\Program Files\FaceAuth\FaceAuthEnroll.exe" identity'
Write-Host 'If the detected qualified name is not MicrosoftAccount\email, rerun with:'
Write-Host '  & "C:\Program Files\FaceAuth\FaceAuthEnroll.exe" identity --qualified "MicrosoftAccount\you@example.com"'
Write-Host 'New enrollments save identity metadata automatically.'
Write-Host 'Then run FaceAuthEnroll.exe test and scripts\test-vault-as-system.ps1.'
Write-Host 'Only after all users pass, run scripts\enable-provider.ps1.'
