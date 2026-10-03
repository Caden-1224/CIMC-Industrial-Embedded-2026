param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]

function Assert-FileContains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )
    $fullPath = Join-Path $Root $Path
    if (-not (Test-Path -LiteralPath $fullPath)) {
        $failures.Add("Missing file: $Path")
        return
    }
    if (-not (Select-String -LiteralPath $fullPath -Pattern $Pattern -Quiet)) {
        $failures.Add($Message)
    }
}

function Assert-FileNotContains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )
    $fullPath = Join-Path $Root $Path
    if ((Test-Path -LiteralPath $fullPath) -and
        (Select-String -LiteralPath $fullPath -Pattern $Pattern -Quiet)) {
        $failures.Add($Message)
    }
}

Assert-FileContains 'CIMC_BootLoader\common\bl_partition.h' 'BL_APP1_START_ADDR\s+0x08011000UL' 'BootLoader partition header is missing official APP address.'
Assert-FileContains 'CIMC_BootLoader\common\bl_param.h' 'BL_PARAM_BACKUP_ADDR' 'BootLoader parameter header is missing backup parameter copy.'
Assert-FileContains 'CIMC_BootLoader\Function\bl_core.c' 'void bootloader_run\(void\)' 'BootLoader core entry must live in Function after CIMC layer alignment.'
Assert-FileContains 'CIMC_BootLoader\Function\Function.c' 'bootloader_run\(\)' 'BootLoader main flow does not call micu bootloader_run().'
Assert-FileContains 'CIMC_BootLoader\Protocol\cimc_protocol.c' 'CimcProtocol_Crc16Modbus' 'BootLoader protocol layer is missing CRC16-Modbus support.'
Assert-FileContains 'CIMC_BootLoader\Protocol\cimc_protocol.c' 'CimcProtocol_Crc32Calc' 'BootLoader protocol layer is missing CRC32 support.'
Assert-FileContains 'CIMC_App\Protocol\cimc_protocol.c' 'CimcProtocol_Crc16Modbus' 'App protocol layer is missing CRC16-Modbus support.'
Assert-FileContains 'CIMC_App\Protocol\ota_protocol.c' 'OtaProtocol_IsStreamHeaderValid' 'App OTA header validation must live in Protocol.'
Assert-FileContains 'CIMC_App\Function\BootUpdate_service.c' 'ring_buffer_' 'App OTA service is not using streaming ring buffer.'
Assert-FileContains 'CIMC_App\Function\BootUpdate_service.c' 'OtaProtocol_IsStreamHeaderValid' 'App OTA service must delegate package-header validation to Protocol.'
Assert-FileNotContains 'CIMC_App\Function\BootUpdate_service.c' 's_raw_image_buffer\[BOOT_APP_SIZE\]' 'App OTA service still keeps a full APP image in RAM.'
Assert-FileNotContains 'CIMC_App\Function\BootUpdate_service.c' 'static uint32_t crc32_calc' 'App OTA service still owns CRC32 instead of Protocol.'
Assert-FileContains 'CIMC_App\common\ota_package_config.h' 'OTA_PACKAGE_MAX_SIZE\s+0x00020000UL' 'OTA package max size is not aligned to the official download cache.'
Assert-FileContains 'CIMC_App\project\CIMC_App.uvprojx' 'Components\\ringbuffer\\ring_buffer.c' 'App Keil project does not include ring_buffer.c.'
Assert-FileContains 'CIMC_App\project\CIMC_App.uvprojx' 'Protocol\\cimc_protocol.c' 'App Keil project does not include cimc_protocol.c.'
Assert-FileContains 'CIMC_App\project\CIMC_App.uvprojx' 'Protocol\\ota_protocol.c' 'App Keil project does not include ota_protocol.c.'
Assert-FileContains 'CIMC_BootLoader\project\CIMC_BootLoader.uvprojx' 'Function\\bl_core.c' 'BootLoader Keil project does not include Function bl_core.c.'
Assert-FileContains 'CIMC_BootLoader\project\CIMC_BootLoader.uvprojx' 'Protocol\\cimc_protocol.c' 'BootLoader Keil project does not include cimc_protocol.c.'
Assert-FileNotContains 'CIMC_BootLoader\project\CIMC_BootLoader.uvprojx' '\.\.\\BOOTLOADER\\' 'BootLoader Keil project still references the old BOOTLOADER directory.'
Assert-FileNotContains 'CIMC_BootLoader\project\CIMC_BootLoader.uvprojx' 'BOOTLOADER\\(Src|Inc)' 'BootLoader Keil project still references old BOOTLOADER Src/Inc files.'

if ($failures.Count -gt 0) {
    Write-Host 'Boot migration verification failed:'
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host 'Boot migration verification passed.'
