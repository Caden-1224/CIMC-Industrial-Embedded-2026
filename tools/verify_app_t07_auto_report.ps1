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
$autoC = Join-Path $root 'CIMC_App\Function\AutoReport_service.c'
$autoH = Join-Path $root 'CIMC_App\Function\AutoReport_service.h'
$rs485C = Join-Path $root 'CIMC_App\Function\Rs485_service.c'
$rs485H = Join-Path $root 'CIMC_App\Function\Rs485_service.h'
$functionC = Join-Path $root 'CIMC_App\Function\Function.c'
$oledC = Join-Path $root 'CIMC_App\Function\Oled_service.c'
$uvproj = Join-Path $root 'CIMC_App\project\CIMC_App.uvprojx'

foreach ($file in @($autoC, $autoH, $rs485C, $rs485H, $functionC, $oledC, $uvproj)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $autoC 'AUTO_REPORT_COMMAND\s+0x0302U' 'Auto report service must use command 0x0302.'
Assert-Contains $autoC 'AUTO_REPORT_CONTENT_LEN\s+12U' 'Auto report payload length must be 12 bytes.'
Assert-Contains $autoC 'Rs485_Service_GetCurrentTimestamp\(\)' 'Auto report must include RTC timestamp.'
Assert-Contains $autoC 'APP_PARAM_CHANNEL_CH0' 'Auto report must read CH0.'
Assert-Contains $autoC 'APP_PARAM_CHANNEL_CH1' 'Auto report must read CH1.'
Assert-Contains $autoC 'CimcProtocol_WriteFloatBe\(&content\[4\]' 'CH0 must be written as big-endian float at payload[4..7].'
Assert-Contains $autoC 'CimcProtocol_WriteFloatBe\(&content\[8\]' 'CH1 must be written as big-endian float at payload[8..11].'
Assert-Contains $autoC 'return 1000UL' 'Missing 1s report interval.'
Assert-Contains $autoC 'return 3000UL' 'Missing 3s report interval.'
Assert-Contains $autoC 'return 5000UL' 'Missing 5s report interval.'
Assert-Contains $autoC 'led_state\[AUTO_REPORT_SAMPLE_LED_INDEX\]' 'Auto report must control sample LED state.'

Assert-Contains $rs485C '#include "AutoReport_service.h"' 'RS485 service must include AutoReport_service.'
Assert-Contains $rs485C 'case 0x0302U:' 'RS485 service must handle 0x0302.'
Assert-Contains $rs485C 'AutoReport_Service_Start\(\)' '0x0302 must start auto report.'
Assert-Contains $rs485C 'case 0x0303U:' 'RS485 service must handle 0x0303.'
Assert-Contains $rs485C 'AutoReport_Service_Stop\(\)' '0x0303 must stop auto report.'
Assert-Contains $rs485C 'Rs485_Service_SendOk\(frame->command\)' '0x0303 must return OK.'
Assert-Contains $rs485C 'AutoReport_Service_IsRunning\(\) != 0U[\s\S]*frame->frame_type == CIMC_FRAME_TYPE_COMMAND[\s\S]*frame->command == 0x0303U[\s\S]*Rs485_Service_HandleControlCommand\(frame\)' 'Auto report must handle only 0x0303 before discovery response.'
Assert-Contains $rs485C 'status != CIMC_PARSE_OK[\s\S]*AutoReport_Service_IsRunning\(\) != 0U' 'Parse errors must be silent during auto report.'

Assert-Contains $rs485H 'Rs485_Service_SendContent' 'RS485 header must expose content-frame sender.'
Assert-Contains $rs485H 'Rs485_Service_GetCurrentTimestamp' 'RS485 header must expose timestamp getter.'
Assert-Contains $rs485H 'Rs485_Service_GetChannelValue' 'RS485 header must expose channel value getter.'

Assert-Contains $functionC 'AutoReport_Service_Task' 'Main scheduler must run auto report task.'
Assert-Contains $functionC 'AutoReport_Service_Init' 'System init must initialize auto report service.'
Assert-Contains $oledC 'AutoReport_Service_IsRunning' 'OLED must depend on auto report state.'
Assert-Contains $oledC 'AutoSample' 'OLED must show AutoSample.'
Assert-Contains $uvproj 'AutoReport_service\.c' 'Keil project must include AutoReport_service.c.'
Assert-Contains $uvproj 'AutoReport_service\.h' 'Keil project must include AutoReport_service.h.'

$autoText = Get-Content -LiteralPath $autoC -Raw
if ($autoText -match 'APP_PARAM_CHANNEL_CH2|0x0221|0x0403|0x0413') {
    throw 'T07 auto report service must not depend on CH2/PT100 logic.'
}

Write-Host 'T07 auto report static verification passed.'
