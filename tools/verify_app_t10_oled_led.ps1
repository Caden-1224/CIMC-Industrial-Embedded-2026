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
$oledC = Join-Path $root 'CIMC_App\Function\Oled_service.c'
$ledC = Join-Path $root 'CIMC_App\Function\Led_service.c'
$ledH = Join-Path $root 'CIMC_App\Function\Led_service.h'
$autoC = Join-Path $root 'CIMC_App\Function\AutoReport_service.c'
$functionC = Join-Path $root 'CIMC_App\Function\Function.c'
# 任务拆分计划属于内部开发记录，公开仓库中可能不存在，缺失时跳过相关断言。
$planDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'CIMC*.md' -or $_.Name -like '*任务拆分*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$manualDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs\verification') -File |
    Where-Object { $_.Name -like 'RS485*.md' } |
    Select-Object -First 1 -ExpandProperty FullName

foreach ($file in @($oledC, $ledC, $ledH, $autoC, $functionC, $planDoc, $manualDoc) | Where-Object { $_ }) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $oledC 'OLED_SERVICE_TEAM_LINE\s+"2026397013"' 'OLED first line must show the real team number without prefix.'
Assert-Contains $oledC 'OLED_SERVICE_FONT_SIZE\s+16U' 'OLED scoring page must use 16px text to fill the 128x32 screen.'
Assert-Contains $oledC 'OLED_SERVICE_LINE0_Y\s+0U' 'OLED first big-text line must start at page 0.'
Assert-Contains $oledC 'OLED_SERVICE_LINE1_Y\s+2U' 'OLED second big-text line must start at page 2.'
Assert-Contains $oledC 'OLED_SERVICE_IDLE_LINE\s+"IDLE"' 'OLED must define the APP idle display text.'
Assert-Contains $oledC 'OLED_SERVICE_AUTO_LINE\s+"AutoSample"' 'OLED must define the auto report display text.'
Assert-Contains $oledC 'AutoReport_Service_IsRunning\(\)' 'OLED second line must depend on auto report state.'
Assert-Contains $oledC 'Oled_Service_ShowPaddedLine\(OLED_SERVICE_LINE0_Y,\s*OLED_SERVICE_TEAM_LINE\)' 'OLED line 0 must show the team line.'
Assert-Contains $oledC 'Oled_Service_ShowPaddedLine\(OLED_SERVICE_LINE1_Y,\s*OLED_SERVICE_AUTO_LINE\)' 'OLED line 1 must show AutoSample while reporting.'
Assert-Contains $oledC 'Oled_Service_ShowPaddedLine\(OLED_SERVICE_LINE1_Y,\s*OLED_SERVICE_IDLE_LINE\)' 'OLED line 1 must show IDLE otherwise.'
Assert-NotContains $oledC '"Vol:"|"R:"|"Ohm RDY|ms:%' 'OLED scoring page must not show the old debug voltage/resistance page.'
Assert-NotContains $oledC 'Gd30ad3344_service\.h|gd30ad3344_result' 'OLED scoring page must not depend on PT100/debug values.'

Assert-Contains $ledC 'LED_SERVICE_SYSTEM_LED_INDEX\s+1U' 'LED2 must be reserved for the APP system heartbeat.'
Assert-Contains $ledC 'LED_SERVICE_HEARTBEAT_INTERVAL_MS\s+1000UL' 'APP system heartbeat must use a 1 second interval.'
Assert-Contains $ledC 'Led_Service_UpdateHeartbeat\(\)' 'LED task must update heartbeat before mapping GPIO.'
Assert-Contains $ledC 'get_millis\(\)' 'Heartbeat must use the existing millisecond tick.'
Assert-Contains $ledC 'led_state\[LED_SERVICE_SYSTEM_LED_INDEX\]\s*=\s*\(uint8_t\)\(!led_state\[LED_SERVICE_SYSTEM_LED_INDEX\]\)' 'Heartbeat must toggle the system LED state.'
Assert-Contains $autoC 'AUTO_REPORT_SAMPLE_LED_INDEX\s+0U' 'Auto report acquisition LED must remain LED1.'
Assert-Contains $autoC 'led_state\[AUTO_REPORT_SAMPLE_LED_INDEX\]' 'Auto report service must still control the acquisition LED.'
Assert-Contains $functionC '\{Led_Service_Task,\s*10U,\s*0U\}' 'Scheduler must keep running LED service periodically.'

if ($planDoc) { Assert-Contains $planDoc '### T10' 'Main plan must contain the T10 section.' }
if ($planDoc) { Assert-Contains $planDoc 'tools\\verify_app_t10_oled_led\.ps1' 'Main plan must document the T10 static verification command.' }
Assert-Contains $manualDoc 'T10 OLED' 'Manual checklist must include T10 human verification.'
Assert-Contains $manualDoc '2026397013' 'Manual checklist must document the APP OLED team line.'

Write-Host 'T10 OLED/LED static verification passed.'
