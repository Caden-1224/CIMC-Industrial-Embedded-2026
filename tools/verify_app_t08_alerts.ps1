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

$root = Split-Path -Parent $PSScriptRoot
$alertC = Join-Path $root 'CIMC_App\Function\Alert_service.c'
$alertH = Join-Path $root 'CIMC_App\Function\Alert_service.h'
$rs485C = Join-Path $root 'CIMC_App\Function\Rs485_service.c'
$rs485H = Join-Path $root 'CIMC_App\Function\Rs485_service.h'
$functionC = Join-Path $root 'CIMC_App\Function\Function.c'
$uvproj = Join-Path $root 'CIMC_App\project\CIMC_App.uvprojx'

foreach ($file in @($alertC, $alertH, $rs485C, $rs485H, $functionC, $uvproj)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $alertC 'ALERT_RECORD_COUNT\s+10U' 'Alert history must keep 10 records.'
Assert-Contains $alertC 'ALERT_FLASH_ADDR\s+\(BL_PARAM_PAGE_ADDR \+ 0x00000600UL\)' 'Alert store must live in the reserved parameter-page area.'
Assert-Contains $alertC 'InternalFlash_ErasePage\(BL_PARAM_PAGE_ADDR\)' 'Alert commit must preserve and rewrite the parameter page.'
Assert-Contains $alertC 'Alert_Service_CheckChannel\(APP_PARAM_CHANNEL_CH0\)' 'Alert task must monitor CH0.'
Assert-Contains $alertC 'Alert_Service_CheckChannel\(APP_PARAM_CHANNEL_CH1\)' 'Alert task must monitor CH1.'
Assert-Contains $alertC 'actual > threshold' 'Alert trigger must compare actual value against threshold.'
Assert-Contains $alertC 's_alert_over_threshold\[channel_index\] == 0U' 'Alert trigger must use rising-edge suppression.'
Assert-Contains $alertC 'AppParam_Service_GetAlarmMode\(\) == 0x01U' 'Active alert mode must be checked.'
Assert-Contains $alertC 'Rs485_Service_SendRawText\(line\)' 'Active alert/history must send raw text.'
Assert-Contains $alertC 'Rs485_Service_SendRawText\("empty"\)' 'Empty history must return raw text empty.'
Assert-Contains $alertC 'Alert_Service_CaptureCurrentEdgeBaseline' 'Clear/init must baseline current over-threshold state.'

$alertText = Get-Content -LiteralPath $alertC -Raw
if ($alertText -match 'APP_PARAM_CHANNEL_CH2|0x0221|0x0403|0x0413') {
    throw 'T08 alert service must not depend on CH2/PT100 logic.'
}

Assert-Contains $rs485C '#include "Alert_service.h"' 'RS485 service must include Alert_service.'
Assert-Contains $rs485C 'case 0x0601U:' 'RS485 service must handle 0x0601.'
Assert-Contains $rs485C 'AppParam_Service_SetAlarmMode\(frame->content\[0\]\)' '0x0601 must set alarm mode.'
Assert-Contains $rs485C 'case 0x0602U:' 'RS485 service must handle 0x0602.'
Assert-Contains $rs485C 'Alert_Service_SendHistoryText\(\)' '0x0602 must send alert history as raw text.'
Assert-Contains $rs485C 'case 0x0603U:' 'RS485 service must handle 0x0603.'
Assert-Contains $rs485C 'Alert_Service_Clear\(\)' '0x0603 must clear alert history.'
Assert-Contains $rs485C 'Rs485_Service_SendRawText' 'RS485 service must expose raw text sending.'
Assert-Contains $rs485C 's_raw_text_echo_guard_count' 'Raw text echo guard must exist.'
Assert-Contains $rs485H 'Rs485_Service_SendRawText' 'RS485 header must expose raw text sending.'
Assert-Contains $rs485H 'Rs485_Service_FormatTimestamp' 'RS485 header must expose timestamp formatting.'

Assert-Contains $functionC 'Alert_Service_Task' 'Main scheduler must run alert task.'
Assert-Contains $functionC 'Alert_Service_Init' 'System init must initialize alert service.'
Assert-Contains $uvproj 'Alert_service\.c' 'Keil project must include Alert_service.c.'
Assert-Contains $uvproj 'Alert_service\.h' 'Keil project must include Alert_service.h.'

Write-Host 'T08 alert static verification passed.'
