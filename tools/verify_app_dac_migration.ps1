param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = "Stop"

$appRoot = Join-Path $ProjectRoot "CIMC_App"
$checks = @(
    @{ Path = "Driver\DAC\DAC.h"; Pattern = "void DAC_Init\(void\);"; Name = "DAC hardware header exposes DAC_Init" },
    @{ Path = "Driver\DAC\DAC.h"; Pattern = "void DAC_SetRaw\(uint16_t raw_value\);"; Name = "DAC hardware header exposes raw setter" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "gpio_mode_set\(GPIOA,\s*GPIO_MODE_ANALOG,\s*GPIO_PUPD_NONE,\s*GPIO_PIN_4\);"; Name = "DAC driver configures PA4 analog mode" },
    @{ Path = "Driver\DAC\DAC.c"; Pattern = "dac_data_set\(DAC0,\s*DAC_OUT0,\s*DAC_ALIGN_12B_R"; Name = "DAC driver writes 12-bit right-aligned data" },
    @{ Path = "Function\Dac_service.h"; Pattern = "void Dac_Service_Init\(void\);"; Name = "DAC service header exposes init" },
    @{ Path = "Function\Dac_service.c"; Pattern = "Dac_Service_SetRaw"; Name = "DAC service implements raw output API" },
    @{ Path = "HeaderFiles\HeaderFiles.h"; Pattern = '#include "DAC.h"'; Name = "HeaderFiles includes DAC driver" },
    @{ Path = "HeaderFiles\HeaderFiles.h"; Pattern = '#include "Dac_service.h"'; Name = "HeaderFiles includes DAC service" },
    @{ Path = "Function\Function.h"; Pattern = '#include "Dac_service.h"'; Name = "Function includes DAC service" },
    @{ Path = "Function\Function.c"; Pattern = "Dac_Service_Init\(\);"; Name = "System_Init initializes DAC service" },
    @{ Path = "project\CIMC_App.uvprojx"; Pattern = "\.\.\\Driver\\DAC\\DAC\.c"; Name = "Keil project includes DAC.c" },
    @{ Path = "project\CIMC_App.uvprojx"; Pattern = "\.\.\\Function\\Dac_service\.c"; Name = "Keil project includes Dac_service.c" },
    @{ Path = "project\CIMC_App.uvprojx"; Pattern = "\.\.\\Driver\\DAC"; Name = "Keil include path includes Driver\\DAC" }
)

$failures = @()

foreach ($check in $checks) {
    $file = Join-Path $appRoot $check.Path
    if (-not (Test-Path -LiteralPath $file)) {
        $failures += "Missing file: $($check.Path) [$($check.Name)]"
        continue
    }

    $content = Get-Content -LiteralPath $file -Raw
    if (-not [regex]::IsMatch($content, $check.Pattern)) {
        $failures += "Missing pattern in $($check.Path): $($check.Name)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "DAC migration verification failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "DAC migration verification passed."
