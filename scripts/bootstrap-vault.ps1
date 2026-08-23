# SPDX-License-Identifier: GPL-3.0-only
#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'

$probe = 'C:\Program Files\FaceAuth\FaceAuthVaultProbe.exe'
$log = 'C:\ProgramData\FaceAuth\bootstrap-vault.log'
if (-not (Test-Path $probe)) { throw "Vault probe is not installed: $probe" }

# Fail early with a useful message. In Hyper-V, a Generation 2 VM must have vTPM enabled.
try {
    $tpm = Get-Tpm
    if (-not $tpm.TpmPresent) {
        throw 'No TPM is visible to this Windows instance. If this is a Hyper-V VM, shut it down and enable its virtual TPM (vTPM) on the host, then boot it again.'
    }
    if (-not $tpm.TpmReady) {
        throw 'A TPM is present but Windows reports TpmReady=False. Do not clear the TPM. Make sure TPM auto-provisioning is enabled and reboot the test VM/PC before retrying.'
    }
}
catch [System.Management.Automation.CommandNotFoundException] {
    Write-Warning 'Get-Tpm is unavailable; continuing with the CNG provider bootstrap and relying on its diagnostic log.'
}

Remove-Item -Force -ErrorAction SilentlyContinue $log

$taskName = 'FaceAuth-VaultBootstrap-' + [guid]::NewGuid().ToString('N')
# Run through cmd.exe so stdout/stderr from the SYSTEM process are preserved.
$cmdArgs = '/d /s /c ""{0}" --bootstrap > "{1}" 2>&1"' -f $probe, $log
$action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\cmd.exe" -Argument $cmdArgs
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

    if (-not $ran -or $scheduled.State -eq 'Running') {
        throw "SYSTEM TPM bootstrap timed out. Diagnostic log: $log"
    }

    $detail = ''
    if (Test-Path $log) {
        $rawDetail = Get-Content -Raw -ErrorAction SilentlyContinue $log
        if ($null -ne $rawDetail) {
            $detail = $rawDetail.Trim()
        }
    }

    if ($info.LastTaskResult -ne 0) {
        if ($detail) {
            throw "SYSTEM TPM bootstrap failed with exit code $($info.LastTaskResult).`n$detail`nDiagnostic log: $log"
        }
        throw "SYSTEM TPM bootstrap failed with exit code $($info.LastTaskResult), but no diagnostic output was captured. Diagnostic log: $log"
    }

    if ($detail) { Write-Host $detail }
    Write-Host 'TPM vault initialized as SYSTEM. Private key is non-exportable and SYSTEM-only.' -ForegroundColor Green
}
finally {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
}
