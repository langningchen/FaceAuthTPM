#Requires -RunAsAdministrator
# SPDX-License-Identifier: GPL-3.0-only
param([string]$Sid)
$ErrorActionPreference = 'Stop'

$probe = 'C:\Program Files\FaceAuth\FaceAuthVaultProbe.exe'
$dataRoot = 'C:\ProgramData\FaceAuth'
$log = Join-Path $dataRoot 'vault-probe.log'

if (-not (Test-Path -LiteralPath $probe)) { throw "Vault probe is not installed: $probe" }
if (-not $Sid) { $Sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value }
if ($Sid -notmatch '^S-1-') { throw "Invalid SID: $Sid" }

$credential = Join-Path (Join-Path $dataRoot 'credentials') ($Sid + '.fav')
Write-Host "Testing SID: $Sid"
Write-Host "Credential blob: $credential"
if (-not (Test-Path -LiteralPath $credential)) {
    throw "No encrypted credential exists for SID $Sid. Re-run FaceAuthEnroll.exe enroll as that Windows user."
}

Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue

# Execute the probe directly. The probe writes its own UTF-16 diagnostic result
# file, so Task Scheduler/cmd.exe stdout redirection cannot hide the real CNG
# error or collide with Win32 error code 3.
$taskName = 'FaceAuth-VaultProbe-' + [guid]::NewGuid().ToString('N')
$arguments = ('--sid "{0}" --result-file "{1}"' -f $Sid, $log)
$action = New-ScheduledTaskAction -Execute $probe -Argument $arguments
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

    # Refresh after the task leaves Running state.
    $info = Get-ScheduledTaskInfo -TaskName $taskName

    $detail = ''
    if (Test-Path -LiteralPath $log) {
        $raw = Get-Content -LiteralPath $log -Raw -ErrorAction SilentlyContinue
        if ($null -ne $raw) { $detail = $raw.Trim() }
    }

    if ($info.LastTaskResult -ne 0) {
        if ($detail) {
            throw "SYSTEM vault probe failed with exit code $($info.LastTaskResult).`n$detail`nDiagnostic log: $log"
        }
        throw "SYSTEM vault probe failed with exit code $($info.LastTaskResult), and the probe did not create a result file. This usually means Task Scheduler could not launch the EXE. Log: $log"
    }

    if (-not $detail) {
        throw "SYSTEM vault probe returned success but did not create its result file: $log"
    }
    Write-Host $detail
    Write-Host "TPM vault decrypt succeeded as SYSTEM for $Sid. The password was never printed." -ForegroundColor Green
}
finally {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
}
