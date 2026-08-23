# SPDX-License-Identifier: GPL-3.0-only
# FaceAuthTPM command-line configuration and recovery tool.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidateSet('help','status','enroll','identity','test','vault-test','enable','disable','camera-benchmark','camera-set','logs','uninstall-runtime','purge-data')]
    [string]$Command = 'help',

    [string]$Qualified,
    [ValidateSet('msmf','dshow','any')]
    [string]$Backend = 'msmf',
    [int]$Camera = 0,
    [int]$Repeat = 3,
    [int]$CooldownMs = 5000,
    [int]$Width = 0,
    [int]$Height = 0,
    [int]$Fps = 30
)

$ErrorActionPreference = 'Stop'
$Install = Join-Path $env:ProgramFiles 'FaceAuth'
$Data = Join-Path $env:ProgramData 'FaceAuth'
$ProviderGuid = '{4CF34D82-0D5D-4A5C-9E46-64C16F348C62}'
$ProviderKey = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\$ProviderGuid"
$Dll = Join-Path $Install 'FaceAuthCredentialProvider.dll'
$EnrollExe = Join-Path $Install 'FaceAuthEnroll.exe'
$ProbeExe = Join-Path $Install 'FaceAuthCameraProbe.exe'
$VaultProbeExe = Join-Path $Install 'FaceAuthVaultProbe.exe'

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = [Security.Principal.WindowsPrincipal]::new($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-Admin {
    if (-not (Test-Admin)) {
        throw 'Run Windows PowerShell as Administrator, then run this command again.'
    }
}

function Require-Installed {
    if (-not (Test-Path -LiteralPath $Install)) { throw "FaceAuthTPM is not installed: $Install" }
    if (-not (Test-Path -LiteralPath $EnrollExe)) { throw "FaceAuthEnroll.exe is missing from $Install" }
}

function Invoke-Checked {
    param([Parameter(Mandatory=$true)][string]$FilePath, [string[]]$Arguments=@())
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')" }
}

function Get-EnrolledSids {
    $profiles = @{}
    $credentials = @{}
    $identities = @{}
    foreach ($f in @(Get-ChildItem (Join-Path $Data 'profiles') -Filter '*.fap' -ErrorAction SilentlyContinue)) { $profiles[$f.BaseName] = $true }
    foreach ($f in @(Get-ChildItem (Join-Path $Data 'credentials') -Filter '*.fav' -ErrorAction SilentlyContinue)) { $credentials[$f.BaseName] = $true }
    foreach ($f in @(Get-ChildItem (Join-Path $Data 'identities') -Filter '*.fai' -ErrorAction SilentlyContinue)) { $identities[$f.BaseName] = $true }
    return @($profiles.Keys | Where-Object { $credentials.ContainsKey($_) -and $identities.ContainsKey($_) } | Sort-Object)
}

function Show-Help {
@'
FaceAuthTPM command-line configuration

Usage:
  .\FaceAuthTPM-Configure.ps1 <command> [options]

Commands:
  status              Show install/service/provider/enrollment state.
  enroll              Capture the current user's face and store the encrypted password.
  identity            Refresh identity metadata. Use -Qualified if auto-detection is wrong.
  test                Test face matching for the current user without logging in.
  vault-test           Verify TPM decryption as LocalSystem without printing the password.
  enable              Register the Credential Provider after enrollment/tests pass.
  disable             Unregister FaceAuth immediately; Windows Password/Hello remain untouched.
  camera-benchmark    Benchmark camera startup using the installed probe.
  camera-set          Save camera backend and optional media type.
  logs                Print recent FaceAuth startup logs.
  purge-data           Delete FaceAuth enrollment data after disabling the provider.

Examples:
  .\FaceAuthTPM-Configure.ps1 enroll
  .\FaceAuthTPM-Configure.ps1 identity -Qualified 'MicrosoftAccount\you@example.com'
  .\FaceAuthTPM-Configure.ps1 test
  .\FaceAuthTPM-Configure.ps1 vault-test
  .\FaceAuthTPM-Configure.ps1 camera-benchmark -Backend msmf -Repeat 3 -CooldownMs 5000
  .\FaceAuthTPM-Configure.ps1 camera-set -Backend msmf -Width 2560 -Height 1440 -Fps 30
  .\FaceAuthTPM-Configure.ps1 enable
  .\FaceAuthTPM-Configure.ps1 disable

Important:
  FaceAuthTPM is not Windows Hello. This release supports Microsoft-account Windows
  identities only. It uses an RGB camera and a stored TPM-wrapped Microsoft-account
  password. Keep Windows Password/Hello as recovery paths.
'@ | Write-Host
}

function Show-Status {
    Require-Admin
    $installed = Test-Path -LiteralPath $Dll
    $service = Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
    $providerEnabled = Test-Path -LiteralPath $ProviderKey
    $sids = if (Test-Path -LiteralPath $Data) { @(Get-EnrolledSids) } else { @() }
    $backendFile = Join-Path $Data 'camera-backend.txt'
    $formatFile = Join-Path $Data 'camera-format.txt'

    Write-Host "Install path:       $Install"
    Write-Host "Binaries installed: $installed"
    Write-Host ("Model service:      " + $(if ($service) { $service.Status } else { 'Not installed' }))
    Write-Host "Provider enabled:   $providerEnabled"
    Write-Host "Complete enrollments: $($sids.Count)"
    foreach ($sid in $sids) { Write-Host "  $sid" }
    Write-Host ("Camera backend:     " + $(if (Test-Path $backendFile) { (Get-Content $backendFile -Raw).Trim() } else { '(default: msmf first)' }))
    Write-Host ("Camera format:      " + $(if (Test-Path $formatFile) { (Get-Content $formatFile -Raw).Trim() } else { '(native/default)' }))
}

function Test-VaultAsSystem {
    Require-Admin
    Require-Installed
    if (-not (Test-Path -LiteralPath $VaultProbeExe)) { throw "Vault probe is missing: $VaultProbeExe" }

    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $credential = Join-Path (Join-Path $Data 'credentials') ($sid + '.fav')
    if (-not (Test-Path -LiteralPath $credential)) { throw "No encrypted credential exists for current SID $sid. Run enroll first." }

    $log = Join-Path $Data 'vault-probe.log'
    Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
    $taskName = 'FaceAuth-VaultProbe-' + [guid]::NewGuid().ToString('N')
    $arguments = ('--sid "{0}" --result-file "{1}"' -f $sid, $log)
    $action = New-ScheduledTaskAction -Execute $VaultProbeExe -Argument $arguments
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $task = New-ScheduledTask -Action $action -Principal $principal -Settings $settings

    try {
        Register-ScheduledTask -TaskName $taskName -InputObject $task -Force | Out-Null
        $before = Get-Date
        Start-ScheduledTask -TaskName $taskName
        $deadline = $before.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 250
            $scheduled = Get-ScheduledTask -TaskName $taskName
            $info = Get-ScheduledTaskInfo -TaskName $taskName
            $ran = $info.LastRunTime -ge $before.AddSeconds(-1)
        } while ((-not $ran -or $scheduled.State -eq 'Running') -and (Get-Date) -lt $deadline)
        if (-not $ran -or $scheduled.State -eq 'Running') { throw 'SYSTEM vault probe timed out.' }

        $info = Get-ScheduledTaskInfo -TaskName $taskName
        $detail = ''
        if (Test-Path -LiteralPath $log) {
            $raw = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
            if ($null -ne $raw) { $detail = $raw.Trim() }
        }
        if ($info.LastTaskResult -ne 0) {
            if ($detail) { throw "SYSTEM vault probe failed with exit code $($info.LastTaskResult).`n$detail" }
            throw "SYSTEM vault probe failed with exit code $($info.LastTaskResult)."
        }
        if (-not $detail) { throw 'SYSTEM vault probe returned success but produced no diagnostic result.' }
        Write-Host $detail
        Write-Host "TPM vault decrypt succeeded as SYSTEM for $sid. The password was not printed." -ForegroundColor Green
    }
    finally {
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    }
}

function Enable-Provider {
    Require-Admin
    Require-Installed
    if (Test-Path (Join-Path $Install 'VM_TEST_BUILD_DO_NOT_INSTALL_ON_HOST.txt')) {
        throw 'Refusing to enable the RDP-enabled VM test build on a normal installation.'
    }
    if (-not (Test-Path (Join-Path $Data 'vault-public.blob'))) { throw 'TPM vault is not initialized. Reinstall FaceAuthTPM.' }
    $sids = @(Get-EnrolledSids)
    if ($sids.Count -eq 0) { throw 'No complete enrollment exists. Run enroll, test, and vault-test first.' }
    $service = Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
    if (-not $service) { throw 'FaceAuthModel service is not installed. Reinstall FaceAuthTPM.' }
    if ($service.Status -ne 'Running') { Start-Service FaceAuthModel }
    Invoke-Checked "$env:SystemRoot\System32\regsvr32.exe" @('/s', $Dll)
    Write-Host 'FaceAuth Credential Provider enabled. Test with Win+L before reboot/sign-out.' -ForegroundColor Green
}

function Disable-Provider {
    Require-Admin
    if (Test-Path -LiteralPath $Dll) {
        & "$env:SystemRoot\System32\regsvr32.exe" /u /s $Dll
    }
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $ProviderKey
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$ProviderGuid"
    Write-Host 'FaceAuth Credential Provider disabled. Windows Password/Hello providers were not changed.' -ForegroundColor Green
}

function Camera-Benchmark {
    Require-Admin
    Require-Installed
    if (-not (Test-Path $ProbeExe)) { throw "Camera probe is missing: $ProbeExe" }
    $args = @('--camera', "$Camera", '--repeat', "$Repeat", '--backend', $Backend, '--cooldown-ms', "$CooldownMs")
    if (($Width -gt 0) -xor ($Height -gt 0)) { throw 'Specify both -Width and -Height, or neither.' }
    if ($Width -gt 0) { $args += @('--width', "$Width", '--height', "$Height", '--fps', "$Fps") }
    Invoke-Checked $ProbeExe $args
}

function Camera-Set {
    Require-Admin
    Require-Installed
    New-Item -ItemType Directory -Force $Data | Out-Null
    Set-Content -LiteralPath (Join-Path $Data 'camera-backend.txt') -Value $Backend -Encoding ascii
    if (($Width -gt 0) -xor ($Height -gt 0)) { throw 'Specify both -Width and -Height, or neither.' }
    if ($Width -gt 0) {
        Set-Content -LiteralPath (Join-Path $Data 'camera-format.txt') -Value "$Width $Height $Fps" -Encoding ascii
    } else {
        Set-Content -LiteralPath (Join-Path $Data 'camera-format.txt') -Value '0 0 0' -Encoding ascii
    }
    Write-Host "Camera backend saved: $Backend" -ForegroundColor Green
    Write-Host ("Camera format saved: " + $(if ($Width -gt 0) { "$Width x $Height @ $Fps" } else { 'native/default' }))
}

function Show-Logs {
    Require-Admin
    foreach ($name in @('model-service.log','provider-startup.log','sensor-startup.log','vault-probe.log')) {
        $path = Join-Path $Data $name
        if (Test-Path -LiteralPath $path) {
            Write-Host "`n=== $name ===" -ForegroundColor Cyan
            Get-Content -LiteralPath $path -Tail 80
        }
    }
}

function Uninstall-Runtime {
    Require-Admin
    Disable-Provider
    $service = Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne 'Stopped') { Stop-Service -Name 'FaceAuthModel' -Force -ErrorAction SilentlyContinue }
    if ($service) { & sc.exe delete FaceAuthModel | Out-Null }
    Write-Host 'FaceAuth runtime unregistered. Enrollment data is preserved.' -ForegroundColor Green
}

function Purge-Data {
    Require-Admin
    Disable-Provider
    $service = Get-Service -Name 'FaceAuthModel' -ErrorAction SilentlyContinue
    if ($service -and $service.Status -ne 'Stopped') { Stop-Service -Name 'FaceAuthModel' -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $Data) {
        Remove-Item -LiteralPath $Data -Recurse -Force
    }
    Write-Host 'FaceAuth enrollment/profile/configuration data deleted.' -ForegroundColor Yellow
}

switch ($Command) {
    'help' { Show-Help }
    'status' { Show-Status }
    'enroll' { Require-Admin; Require-Installed; Invoke-Checked $EnrollExe @('enroll') }
    'identity' {
        Require-Admin; Require-Installed
        $args = @('identity')
        if ($Qualified) { $args += @('--qualified', $Qualified) }
        Invoke-Checked $EnrollExe $args
    }
    'test' { Require-Admin; Require-Installed; Invoke-Checked $EnrollExe @('test') }
    'vault-test' { Test-VaultAsSystem }
    'enable' { Enable-Provider }
    'disable' { Disable-Provider }
    'camera-benchmark' { Camera-Benchmark }
    'camera-set' { Camera-Set }
    'logs' { Show-Logs }
    'uninstall-runtime' { Uninstall-Runtime }
    'purge-data' { Purge-Data }
}
