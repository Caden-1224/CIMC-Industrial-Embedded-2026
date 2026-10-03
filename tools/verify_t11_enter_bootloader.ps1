$ErrorActionPreference = 'Stop'

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Message
    )

    $text = Get-Content -LiteralPath $Path -Raw
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

    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -match $Pattern) {
        throw $Message
    }
}

$root = Split-Path -Parent $PSScriptRoot
$appRs485C = Join-Path $root 'CIMC_App\Function\Rs485_service.c'
$appBootC = Join-Path $root 'CIMC_App\Function\BootUpdate_service.c'
$appBootH = Join-Path $root 'CIMC_App\Function\BootUpdate_service.h'
$appBlParamH = Join-Path $root 'CIMC_App\common\bl_param.h'
$bootBlParamH = Join-Path $root 'CIMC_BootLoader\common\bl_param.h'
$bootCoreC = Join-Path $root 'CIMC_BootLoader\Function\bl_core.c'
$bootFunctionC = Join-Path $root 'CIMC_BootLoader\Function\Function.c'
$bootUsartC = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.c'
$bootOledC = Join-Path $root 'CIMC_BootLoader\Driver\OLED_IIC\bl_oled.c'
$bootOledH = Join-Path $root 'CIMC_BootLoader\Driver\OLED_IIC\bl_oled.h'
$bootUvproj = Join-Path $root 'CIMC_BootLoader\project\CIMC_BootLoader.uvprojx'
# 任务拆分计划属于内部开发记录，公开仓库中可能不存在，缺失时跳过相关断言。
$planDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'CIMC*.md' -or $_.Name -like '*任务拆分*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$manualDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs\verification') -File |
    Where-Object { $_.Name -like 'RS485*.md' } |
    Select-Object -First 1 -ExpandProperty FullName

foreach ($file in @($appRs485C, $appBootC, $appBootH, $appBlParamH, $bootBlParamH, $bootCoreC, $bootFunctionC, $bootUsartC, $bootOledC, $bootOledH, $bootUvproj, $planDoc, $manualDoc) | Where-Object { $_ }) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $appBlParamH 'BL_UPDATE_FLAG_FAST_UPGRADE\s+0x55AA55AAUL' 'APP shared parameter header must define the fast-upgrade intent flag.'
Assert-Contains $bootBlParamH 'BL_UPDATE_FLAG_FAST_UPGRADE\s+0x55AA55AAUL' 'BootLoader shared parameter header must define the same fast-upgrade intent flag.'

Assert-Contains $appBootH 'BootUpdate_Service_RequestEnterBootloader' 'APP boot update service must expose the 0x0501 request API.'
Assert-Contains $appBootC 'BootUpdate_Service_RequestEnterBootloader' 'APP boot update service must implement the 0x0501 request API.'
Assert-Contains $appBootC 'BL_UPDATE_FLAG_FAST_UPGRADE' 'APP 0x0501 path must write the fast-upgrade intent flag, not the pending-install flag.'
Assert-Contains $appBootC 'NVIC_SystemReset\(\)' 'APP 0x0501 path must soft-reset after sending OK.'
Assert-Contains $appRs485C '#include "BootUpdate_service.h"' 'RS485 service must include BootUpdate_service.'
Assert-Contains $appRs485C 'case 0x0501U:' 'RS485 service must dispatch command 0x0501.'
Assert-Contains $appRs485C 'BootUpdate_Service_RequestEnterBootloader\(\)' '0x0501 handler must request Bootloader entry.'
Assert-Contains $appRs485C 'Rs485_Service_SendOk\(frame->command\)' '0x0501 handler must reply OK before reset.'
Assert-Contains $appRs485C 'RS485_BOOTLOADER_ENTRY_DELAY_MS\s+1500U' '0x0501 must delay BootLoader entry long enough to keep the OK frame separate from the raw BootLoader banner.'
Assert-Contains $appRs485C 'Rs485_Service_RequestBootloaderEntryReset\(\)' '0x0501 must use the upgrade-specific reset scheduler.'
Assert-Contains $appRs485C 's_startup_heartbeat_pending\s*=\s*0U' '0x0501 must suppress optional APP startup heartbeat before entering BootLoader.'

Assert-Contains $bootOledH 'BlOled_Init' 'BootLoader OLED driver must expose an init API.'
Assert-Contains $bootOledH 'BlOled_ShowBootloader' 'BootLoader OLED driver must expose the Bootloader display API.'
Assert-Contains $bootOledC '2026397013' 'BootLoader OLED first line must keep the team number visible.'
Assert-Contains $bootOledC 'Bootloader' 'BootLoader OLED must display the required Bootloader state text.'
Assert-Contains $bootOledC 's_font8x16' 'BootLoader OLED must use the same 8x16 font shape as the APP scoring page.'
Assert-Contains $bootOledC 'bl_oled_show_char8x16' 'BootLoader OLED must draw 8-pixel-wide 16px characters, not stretched 6x8 glyphs.'
Assert-NotContains $bootOledC 's_font6x8|bl_oled_scale_column|bl_oled_show_char16' 'BootLoader OLED must not use the old stretched 6x8 font.'
Assert-Contains $bootFunctionC '#include "bl_oled.h"' 'BootLoader system init must include the OLED driver.'
Assert-Contains $bootFunctionC 'BlOled_Init\(\)' 'BootLoader system init must initialize OLED.'
Assert-Contains $bootFunctionC 'BlOled_ShowBootloader\(\)' 'BootLoader OLED must display Bootloader whenever code is running in the BootLoader area, including the normal silent 5s startup window.'
Assert-Contains $bootUvproj '..\\Driver\\OLED_IIC' 'BootLoader Keil include path must contain the OLED driver folder.'
Assert-Contains $bootUvproj 'bl_oled\.c' 'BootLoader Keil project must compile bl_oled.c.'
Assert-Contains $bootUvproj 'bl_oled\.h' 'BootLoader Keil project must include bl_oled.h.'

Assert-Contains $bootCoreC 'BL_UPDATE_FLAG_FAST_UPGRADE' 'BootLoader must consume the APP fast-upgrade intent flag.'
Assert-Contains $bootCoreC 'bl_wait_for_upgrade_command' 'BootLoader must have a dedicated 10s upgrade-command wait path.'
Assert-Contains $bootCoreC 'using command to interrupt start Application' 'BootLoader must print the official upgrade prompt text.'
Assert-Contains $bootCoreC 'wait for start Application\(10s\)' 'BootLoader must print the 10s countdown marker.'
Assert-Contains $bootCoreC 'wait for start Application\(7s\)' 'BootLoader must print the 7s countdown marker.'
Assert-Contains $bootCoreC 'wait for start Application\(4s\)' 'BootLoader must print the 4s countdown marker.'
Assert-Contains $bootCoreC 'wait for start Application\(1s\)' 'BootLoader must print the 1s countdown marker.'
Assert-Contains $bootCoreC '0x0502U' 'BootLoader wait path must recognize the prepare-transfer command.'
Assert-Contains $bootCoreC 'delay_1ms\(5000U\)' 'Normal BootLoader startup must still keep the silent 5s APP jump path.'
Assert-NotContains $bootCoreC 'printf\("BL log dump' 'BootLoader must not use the old debug log dump on normal scoring paths.'
Assert-Contains $bootUsartC 'BL_USART_DE_SETTLE_DELAY' 'BootLoader UART must keep an explicit RS485 DE settle delay before the first byte.'
Assert-Contains $bootUsartC 'usart_flag_clear\(USART\s*,\s*USART_FLAG_TC\)' 'BootLoader UART must clear stale TC before each blocking byte send.'
Assert-Contains $bootUsartC 'while\s*\(RESET\s*==\s*usart_flag_get\(USART\s*,\s*USART_FLAG_TBE\)\)' 'BootLoader UART must wait for TBE before writing the byte.'

if ($planDoc) { Assert-Contains $planDoc '### T11' 'Main plan must contain the T11 section.' }
if ($planDoc) { Assert-Contains $planDoc 'tools\\verify_t11_enter_bootloader\.ps1' 'Main plan must document the T11 static verification command.' }
Assert-Contains $manualDoc 'T11' 'Manual checklist must include T11 human verification.'
Assert-Contains $manualDoc 'A5B60001010501000225AAB6A5' 'Manual checklist must include the 0x0501 request frame.'

Write-Host 'T11 enter-Bootloader static verification passed.'
