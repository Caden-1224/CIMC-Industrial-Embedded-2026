param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = "Stop"

$appRoot = Join-Path $ProjectRoot "CIMC_App"
$checks = @(
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_DEFAULT_DEVICE_ID\s+0x0001U"; Name = "APP parameter service keeps default device ID 0001" },
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_DEFAULT_BAUD_CODE\s+0x13U"; Name = "APP parameter service keeps default baud code 19200" },
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_FIRMWARE_VERSION_MAJOR\s+0x02U"; Name = "APP firmware major version is 2" },
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_FIRMWARE_VERSION_MINOR\s+0x00U"; Name = "APP firmware minor version is 0" },
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_FIRMWARE_VERSION_PATCH\s+0x01U"; Name = "APP firmware patch version is 1" },
    @{ Path = "Function\App_param_service.h"; Pattern = "APP_PARAM_FIRMWARE_VERSION_BUILD\s+0x00U"; Name = "APP firmware build version is 0" },
    @{ Path = "Function\App_param_service.c"; Pattern = "AppParam_Service_IsValidBootParam"; Name = "APP parameter service validates Bootloader parameter CRC" },
    @{ Path = "Function\App_param_service.c"; Pattern = "InternalFlash_ErasePage\(BL_PARAM_PAGE_ADDR\)"; Name = "APP parameter service commits full shared parameter page" },
    @{ Path = "Function\App_param_service.c"; Pattern = "BL_PARAM_BACKUP_ADDR"; Name = "APP parameter service updates backup parameter copy" },
    @{ Path = "Function\App_param_service.c"; Pattern = "bl_commit_param\("; Name = "APP must not link against Bootloader-only bl_commit_param"; Forbid = $true },
    @{ Path = "Function\Function.c"; Pattern = "AppParam_Service_Init\(\);"; Name = "System init loads APP parameters before RS485 init" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0101U"; Name = "RS485 handles reset command" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "RS485_RESET_DELAY_MS\s+300U"; Name = "Reset command keeps a conservative ACK-drain delay before software reset" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "RS485_ID_CHANGE_RESET_DELAY_MS\s+700U"; Name = "Device-ID change keeps a longer commit and PC-tool transition delay" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_RequestResetAfter\(RS485_ID_CHANGE_RESET_DELAY_MS\)"; Name = "Device-ID change uses the longer reset delay after ACK" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_SendStartupHeartbeat"; Name = "APP schedules one startup heartbeat for post-reset discovery scoring" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_TaskHeartbeat"; Name = "APP heartbeat scheduling is handled in the RS485 task, not during init" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "s_startup_heartbeat_pending"; Name = "APP startup heartbeat is sent once after RS485 initialization" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0104U"; Name = "RS485 handles firmware version query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0105U"; Name = "RS485 handles RTC set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0106U"; Name = "RS485 handles RTC query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x01A1U"; Name = "RS485 handles device ID set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0111U"; Name = "RS485 handles device ID query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x01A2U"; Name = "RS485 handles baud set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0112U"; Name = "RS485 handles baud query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_UnixSecondsToRtc"; Name = "RS485 converts Unix seconds into RTC BCD time" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_RtcToUnixSeconds"; Name = "RS485 converts RTC BCD time into Unix seconds" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "NVIC_SystemReset\(\)"; Name = "Reset and destructive parameter changes reset after ACK" },
    @{ Path = "project\CIMC_App.uvprojx"; Pattern = "App_param_service\.c"; Name = "Keil project includes App_param_service.c" },
    @{ Path = "project\CIMC_App.uvprojx"; Pattern = "App_param_service\.h"; Name = "Keil project includes App_param_service.h" }
)

$failures = @()

foreach ($check in $checks) {
    $file = Join-Path $appRoot $check.Path
    if (-not (Test-Path -LiteralPath $file)) {
        $failures += "Missing file: $($check.Path) [$($check.Name)]"
        continue
    }

    $content = Get-Content -LiteralPath $file -Raw
    $matched = [regex]::IsMatch($content, $check.Pattern)
    if (($check.Forbid -eq $true) -and $matched) {
        $failures += "Forbidden pattern in $($check.Path): $($check.Name)"
    }
    elseif (($check.Forbid -ne $true) -and (-not $matched)) {
        $failures += "Missing pattern in $($check.Path): $($check.Name)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "APP T03 system command verification failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "APP T03 system command verification passed."
