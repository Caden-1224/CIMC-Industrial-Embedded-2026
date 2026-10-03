param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = "Stop"

$appRoot = Join-Path $ProjectRoot "CIMC_App"
$checks = @(
    @{ Path = "Driver\ADC\ADC.h"; Pattern = "#define\s+ADC_INTERNAL_CH0_INDEX\s+0U"; Name = "ADC header defines CH0 buffer index" },
    @{ Path = "Driver\ADC\ADC.h"; Pattern = "#define\s+ADC_INTERNAL_CH1_INDEX\s+1U"; Name = "ADC header defines CH1 buffer index" },
    @{ Path = "Driver\ADC\ADC.h"; Pattern = "ADC_ReadChannelRaw"; Name = "ADC driver exposes active single-channel sampler" },
    @{ Path = "Driver\ADC\ADC.c"; Pattern = "GPIO_PIN_0\s*\|\s*GPIO_PIN_1"; Name = "ADC GPIO config enables PC0 and PC1 analog input" },
    @{ Path = "Driver\ADC\ADC.c"; Pattern = "adc_channel_length_config\(ADC0,\s*ADC_ROUTINE_CHANNEL,\s*1U\)"; Name = "ADC active sampler uses one regular channel per read" },
    @{ Path = "Driver\ADC\ADC.c"; Pattern = "adc_routine_channel_config\(ADC0,\s*ADC_INTERNAL_CH0_INDEX,\s*ADC_CHANNEL_10"; Name = "ADC rank 0 maps CH0 to PC0/ADC_CHANNEL_10" },
    @{ Path = "Driver\ADC\ADC.c"; Pattern = "ADC_ReadChannelRaw"; Name = "ADC source implements active raw sampling" },
    @{ Path = "Function\Adc_service.h"; Pattern = "Adc_Channel_Result_t\s+ch0;"; Name = "ADC service exposes CH0 result" },
    @{ Path = "Function\Adc_service.h"; Pattern = "Adc_Channel_Result_t\s+ch1;"; Name = "ADC service exposes CH1 result" },
    @{ Path = "Function\Adc_service.c"; Pattern = "ADC_ReadChannelRaw\(ADC_CHANNEL_10\)"; Name = "ADC service actively reads CH0 from PC0/ADC_CHANNEL_10" },
    @{ Path = "Function\Adc_service.c"; Pattern = "ADC_ReadChannelRaw\(ADC_CHANNEL_11\)"; Name = "ADC service actively reads CH1 from PC1/ADC_CHANNEL_11" },
    @{ Path = "Function\Gd30ad3344_service.c"; Pattern = "GD30AD3344_SERVICE_CHANNEL"; Name = "External ADC/PT100 service remains the CH2 source" }
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
    Write-Host "Three-channel mapping verification failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "Three-channel mapping verification passed."
