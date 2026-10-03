param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = "Stop"

$appRoot = Join-Path $ProjectRoot "CIMC_App"
$checks = @(
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_GetRatio"; Name = "APP parameter service exposes channel ratio getter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_SetRatio"; Name = "APP parameter service exposes channel ratio setter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_GetThreshold"; Name = "APP parameter service exposes threshold getter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_SetThreshold"; Name = "APP parameter service exposes threshold setter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_GetReportIntervalCode"; Name = "APP parameter service exposes report interval getter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_SetReportIntervalCode"; Name = "APP parameter service exposes report interval setter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_GetAlarmMode"; Name = "APP parameter service exposes alarm mode getter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_SetAlarmMode"; Name = "APP parameter service exposes alarm mode setter" },

    @{ Path = "Function\App_param_service.c"; Pattern = "APP_PARAM_VERSION\s+0x00020000UL"; Name = "APP parameter block version includes T04/T05 fields" },
    @{ Path = "Function\App_param_service.c"; Pattern = "ch0_ratio"; Name = "APP parameter block stores CH0 ratio" },
    @{ Path = "Function\App_param_service.c"; Pattern = "ch1_ratio"; Name = "APP parameter block stores CH1 ratio" },
    @{ Path = "Function\App_param_service.c"; Pattern = "ch0_threshold"; Name = "APP parameter block stores CH0 threshold" },
    @{ Path = "Function\App_param_service.c"; Pattern = "ch1_threshold"; Name = "APP parameter block stores CH1 threshold" },
    @{ Path = "Function\App_param_service.c"; Pattern = "ch2_threshold"; Name = "APP parameter block stores CH2 threshold" },
    @{ Path = "Function\App_param_service.c"; Pattern = "report_interval_code"; Name = "APP parameter block stores report interval code" },
    @{ Path = "Function\App_param_service.c"; Pattern = "alarm_mode"; Name = "APP parameter block stores alarm mode" },
    @{ Path = "Function\App_param_service.c"; Pattern = "alert_head"; Name = "APP parameter block reserves alert ring-buffer metadata" },
    @{ Path = "Function\App_param_service.c"; Pattern = "APP_PARAM_DEFAULT_FLOAT\s+21\.59f"; Name = "APP parameter service defaults thresholds to PDF example float 21.59" },
    @{ Path = "Function\App_param_service.c"; Pattern = "AppParam_Service_IsValidRatio"; Name = "APP parameter service validates ratios before flash commit" },
    @{ Path = "Function\App_param_service.c"; Pattern = "AppParam_Service_IsValidThreshold"; Name = "APP parameter service validates thresholds before flash commit" },
    @{ Path = "Function\App_param_service.c"; Pattern = "AppParam_Service_IsFiniteFloat"; Name = "APP parameter service rejects NaN and infinite floats" },
    @{ Path = "Function\App_param_service.c"; Pattern = "InternalFlash_ErasePage\(BL_PARAM_PAGE_ADDR\)"; Name = "APP parameter service writes the shared parameter page only through the full-page commit path" },
    @{ Path = "Function\App_param_service.c"; Pattern = "BL_PARAM_BACKUP_ADDR"; Name = "APP parameter service updates backup parameter copy" },
    @{ Path = "Function\App_param_service.c"; Pattern = "bl_commit_param\("; Name = "APP must not link against Bootloader-only bl_commit_param"; Forbid = $true },

    @{ Path = "Function\Rs485_service.c"; Pattern = '#include "Adc_service\.h"'; Name = "RS485 data plane reads APP internal ADC service" },
    @{ Path = "Function\Rs485_service.c"; Pattern = '#include "Gd30ad3344_service\.h"'; Name = "RS485 data plane reads external ADC service" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_GetChannelValue"; Name = "RS485 data plane has one channel value conversion entry" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "adc_result\.ch0\.smooth"; Name = "CH0 query uses board potentiometer ADC result" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "adc_result\.ch1\.smooth"; Name = "CH1 query uses DAC feedback ADC result" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "gd30ad3344_result\.resistance"; Name = "CH2 query is wired to external ADC result until PT100 temperature calibration" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "CimcProtocol_WriteFloatBe"; Name = "RS485 returns float payloads in big-endian IEEE754 format" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_ReadFloatBe"; Name = "RS485 reads big-endian IEEE754 command payloads" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0201U"; Name = "RS485 handles CH0 query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0202U"; Name = "RS485 handles CH1 query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0221U"; Name = "RS485 handles CH2 query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0241U"; Name = "RS485 handles CH0 ratio set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0242U"; Name = "RS485 handles CH1 ratio set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0261U"; Name = "RS485 handles report interval set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0400U"; Name = "RS485 handles threshold batch query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0401U"; Name = "RS485 handles CH0 threshold query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0402U"; Name = "RS485 handles CH1 threshold query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0403U"; Name = "RS485 handles CH2 threshold query" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0411U"; Name = "RS485 handles CH0 threshold set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0412U"; Name = "RS485 handles CH1 threshold set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0413U"; Name = "RS485 handles CH2 threshold set" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "\(frame->command & 0xFF00U\) == 0x0200U"; Name = "Dispatcher routes 0x02 data commands" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "\(frame->command & 0xFF00U\) == 0x0400U"; Name = "Dispatcher routes 0x04 threshold commands" }
)

$failures = @()

foreach ($check in $checks) {
    $file = Join-Path $appRoot $check.Path
    if (-not (Test-Path -LiteralPath $file)) {
        $failures += "Missing file: $($check.Path) [$($check.Name)]"
        continue
    }

    $content = Get-Content -LiteralPath $file -Raw -Encoding UTF8
    $matched = [regex]::IsMatch($content, $check.Pattern)
    if (($check.Forbid -eq $true) -and $matched) {
        $failures += "Forbidden pattern in $($check.Path): $($check.Name)"
    }
    elseif (($check.Forbid -ne $true) -and (-not $matched)) {
        $failures += "Missing pattern in $($check.Path): $($check.Name)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "APP T04/T05 data and parameter verification failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "APP T04/T05 data and parameter verification passed."
