param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = "Stop"

$appRoot = Join-Path $ProjectRoot "CIMC_App"
$checks = @(
    @{ Path = "Driver\DAC\DAC.h"; Pattern = "DAC_OUTPUT_MAX_RAW\s+4095U"; Name = "DAC driver defines 12-bit maximum raw code" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "gpio_mode_set\(GPIOA,\s*GPIO_MODE_ANALOG,\s*GPIO_PUPD_NONE,\s*GPIO_PIN_4\)"; Name = "DAC driver configures PA4 as analog output" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "dac_trigger_disable\(DAC0,\s*DAC_OUT0\)"; Name = "DAC driver uses direct DC output mode without trigger gating" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "dac_data_set\(DAC0,\s*DAC_OUT0,\s*DAC_ALIGN_12B_R,\s*raw_value\)"; Name = "DAC driver writes 12-bit right-aligned output" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "dac_software_trigger_enable\(DAC0,\s*DAC_OUT0\)"; Name = "DAC static output path must not depend on software trigger"; Forbid = $true },
    @{ Path = "Function\Dac_service.h"; Pattern = "Dac_Service_SetRaw"; Name = "DAC service exposes raw output setter" },
    @{ Path = "Function\Dac_service.c"; Pattern = "Dac_Service_SetRaw"; Name = "DAC service implements raw output setter" },
    @{ Path = "Function\Dac_service.c"; Pattern = '#include "App_param_service\.h"'; Name = "DAC service can restore persisted DAC excitation" },
    @{ Path = "Function\Dac_service.c"; Pattern = "DAC_SetRaw\(AppParam_Service_GetDacRaw\(\)\)"; Name = "DAC service restores the persisted raw DAC code at startup" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_GetDacRaw"; Name = "APP parameter service exposes persisted DAC getter" },
    @{ Path = "Function\App_param_service.h"; Pattern = "AppParam_Service_SetDacRaw"; Name = "APP parameter service exposes persisted DAC setter" },
    @{ Path = "Function\App_param_service.c"; Pattern = "uint16_t dac_raw"; Name = "APP parameter block stores DAC raw code" },
    @{ Path = "Function\App_param_service.c"; Pattern = "AppParam_Service_SetDacRaw"; Name = "APP parameter service persists DAC raw code" },
    @{ Path = "Function\Function.c"; Pattern = "Dac_Service_Init\(\);"; Name = "System init brings up DAC service" },
    @{ Path = "Function\Rs485_service.c"; Pattern = '#include "Dac_service\.h"'; Name = "RS485 control command layer depends on DAC service, not the driver" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Rs485_Service_HandleControlCommand"; Name = "RS485 has a 0x03 control command handler" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "case\s+0x0301U"; Name = "RS485 handles DAC output command 0x0301" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "frame->content_len\s*!=\s*2U"; Name = "DAC command rejects non-2-byte payloads" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "CimcProtocol_ReadBe16\(frame->content\)"; Name = "DAC command reads raw value as big-endian u16" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "DAC_OUTPUT_MAX_RAW"; Name = "DAC command rejects values above 0x0FFF" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "AppParam_Service_SetDacRaw\(dac_raw\)"; Name = "DAC command persists accepted raw value before ACK" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "Dac_Service_SetRaw"; Name = "DAC command calls business service setter" },
    @{ Path = "Function\Rs485_service.c"; Pattern = "\(frame->command & 0xFF00U\) == 0x0300U"; Name = "Dispatcher routes 0x03 control commands" }
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
    Write-Host "APP T06 DAC linkage verification failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "APP T06 DAC linkage verification passed."
