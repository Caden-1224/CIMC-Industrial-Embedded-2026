$ErrorActionPreference = 'Stop'

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ($text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-NotContains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ($text -match $Pattern) {
        throw $Message
    }
}

$root = Split-Path -Parent $PSScriptRoot
$bootCoreC = Join-Path $root 'CIMC_BootLoader\Function\bl_core.c'
$appParamC = Join-Path $root 'CIMC_App\Function\App_param_service.c'
$bootFunctionC = Join-Path $root 'CIMC_BootLoader\Function\Function.c'
$bootUsartC = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.c'
$bootUsartH = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.h'
$manualDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs\verification') -File |
    Where-Object { $_.Name -like 'RS485*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$memoryDoc = @((Join-Path $root 'PROJECT_MEMORY.md'),
    (Join-Path $root 'docs\dev-notes\PROJECT_MEMORY.md')) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

foreach ($file in @($bootCoreC, $appParamC, $bootFunctionC, $bootUsartC, $bootUsartH, $manualDoc, $memoryDoc) | Where-Object { $_ }) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $bootCoreC 'APP_PARAM_WORD_OFFSET\s+32U' 'BootLoader must read the APP parameter block from the shared reserved[] offset.'
Assert-Contains $bootCoreC 'APP_PARAM_MAGIC\s+0x43494D43UL' 'BootLoader must validate the APP parameter block magic before trusting ID/baud.'
Assert-Contains $bootCoreC 'APP_PARAM_VERSION\s+0x00020000UL' 'BootLoader must validate the APP parameter block version before trusting ID/baud.'
Assert-Contains $appParamC 'uint16_t dac_raw' 'APP parameter block must include the persisted DAC raw code used by F-02.'
Assert-Contains $bootCoreC 'uint16_t dac_raw' 'BootLoader mirror of the APP parameter block must include dac_raw so the CRC/layout stays synchronized.'
Assert-Contains $bootCoreC 'bl_app_param_is_valid' 'BootLoader must CRC-check the APP parameter block before using official-sequence state.'
Assert-Contains $bootCoreC 'bl_set_runtime_identity_from_param' 'BootLoader must derive runtime device ID and baud from persisted APP parameters.'
Assert-Contains $bootCoreC 's_bl_runtime_device_id' 'BootLoader response frames must use the runtime device ID.'
Assert-Contains $bootCoreC 's_bl_upgrade_baudrate' 'BootLoader must expose the official OTA baudrate selected from APP parameters.'
Assert-Contains $bootCoreC 'APP_PARAM_DEFAULT_DEVICE_ID' 'Invalid APP parameter blocks must fall back to the contest-safe default ID.'
Assert-Contains $bootCoreC 'APP_PARAM_OFFICIAL_OTA_BAUDRATE\s+115200UL' 'Official OTA path must run at 115200 after the M-series baudrate test.'
Assert-Contains $bootCoreC 'frame\.device_id\s*!=\s*s_bl_runtime_device_id' 'BootLoader command parser must accept current persisted ID, not only 0001.'
Assert-Contains $bootCoreC 'frame\.device_id\s*!=\s*CIMC_DEVICE_ID_BROADCAST' 'BootLoader command parser must continue accepting broadcast frames.'
Assert-NotContains $bootCoreC 'CimcProtocol_BuildOkFrame\(0x0001U' 'BootLoader OK frames must not hardcode device ID 0001.'
Assert-NotContains $bootCoreC 'CimcProtocol_BuildErrorFrame\(0x0001U' 'BootLoader ERROR frames must not hardcode device ID 0001.'
Assert-NotContains $bootCoreC 'frame\.device_id\s*!=\s*0x0001U' 'BootLoader command parser must not hardcode device ID 0001.'

Assert-Contains $bootFunctionC 'my_usart_init\(bootloader_get_upgrade_baudrate\(\)\)' 'BootLoader USART must be initialized with the official OTA baudrate selected by bootloader policy.'
Assert-Contains $bootUsartH 'void my_usart_init\(uint32_t baudrate\);' 'USART init API must accept a runtime baudrate.'
Assert-Contains $bootUsartC 'void my_usart_init\(uint32_t baudrate\)' 'USART driver must implement runtime baudrate initialization.'
Assert-Contains $bootUsartC 'usart_baudrate_set\(USART\s*,\s*baudrate\)' 'USART driver must program the caller-provided baudrate.'
Assert-NotContains $bootUsartC 'usart_baudrate_set\(USART\s*,\s*19200U\)' 'BootLoader USART must not be hardcoded to the factory 19200 baudrate.'

Assert-Contains $manualDoc 'L/M/N' 'Manual checklist must document the official L/M/N continuous-sequence state.'
Assert-Contains $manualDoc '115200' 'Manual checklist must remind that N runs after M at 115200.'
Assert-Contains $manualDoc 'ID' 'Manual checklist must remind that N uses the current persisted ID or broadcast.'
if ($memoryDoc) { Assert-Contains $memoryDoc 'T14' 'PROJECT_MEMORY must record the official L/M/N sequence decision.' }
if ($memoryDoc) { Assert-Contains $memoryDoc 'L/M/N' 'PROJECT_MEMORY must record the official L/M/N sequence decision.' }

Write-Host 'T14 official-sequence baudrate/device-id static verification passed.'
