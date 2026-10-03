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
$sleepC = Join-Path $root 'CIMC_App\Function\Sleep_service.c'
$sleepH = Join-Path $root 'CIMC_App\Function\Sleep_service.h'
$rs485C = Join-Path $root 'CIMC_App\Function\Rs485_service.c'
$functionC = Join-Path $root 'CIMC_App\Function\Function.c'
$functionH = Join-Path $root 'CIMC_App\Function\Function.h'
$headerH = Join-Path $root 'CIMC_App\HeaderFiles\HeaderFiles.h'
$itC = Join-Path $root 'CIMC_App\User\gd32f4xx_it.c'
$rs485DmaC = Join-Path $root 'CIMC_App\Driver\RS485\rs485_dma.c'
$oledDriverC = Join-Path $root 'CIMC_App\Driver\OLED_IIC\OLED_IIC.c'
$uvproj = Join-Path $root 'CIMC_App\project\CIMC_App.uvprojx'

foreach ($file in @($sleepC, $sleepH, $rs485C, $functionC, $functionH, $headerH, $itC, $rs485DmaC, $oledDriverC, $uvproj)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $sleepH 'Sleep_Service_RequestRtcWakeup' 'Sleep service must expose the 0x03AA request API.'
Assert-Contains $sleepH 'Sleep_Service_Task' 'Sleep service must expose a scheduler task.'
Assert-Contains $sleepH 'Sleep_Service_RtcAlarmIrqHandler' 'Sleep service must expose the RTC alarm ISR hook.'

Assert-Contains $sleepC 'SLEEP_SERVICE_WAKEUP_DELAY_SECONDS\s+10U' 'Sleep service must use a 10 second RTC alarm.'
Assert-Contains $sleepC 'instrument wakeup' 'Wakeup path must send the raw ASCII wakeup string.'
Assert-Contains $sleepC 'rtc_alarm_config\(RTC_ALARM0' 'Sleep service must configure RTC ALARM0.'
Assert-Contains $sleepC 'rtc_interrupt_enable\(RTC_INT_ALARM0\)' 'Sleep service must enable RTC alarm interrupt.'
Assert-Contains $sleepC 'nvic_irq_enable\(RTC_Alarm_IRQn' 'Sleep service must enable the RTC alarm NVIC line.'
Assert-Contains $sleepC 'pmu_to_deepsleepmode\(PMU_LDO_LOWPOWER' 'Sleep service must enter MCU deep sleep, not standby reset.'
Assert-Contains $sleepC 'SystemInit\(\)' 'Sleep wakeup path must restore system clock configuration.'
Assert-Contains $sleepC 'SystemCoreClockUpdate\(\)' 'Sleep wakeup path must refresh SystemCoreClock.'
Assert-Contains $sleepC 'systick_config\(\)' 'Sleep wakeup path must restore SysTick after deep sleep.'
Assert-Contains $sleepC 'OLED_Display_Off\(\)' 'Sleep entry must turn off the OLED panel before WFI.'
Assert-Contains $sleepC 'Oled_Service_Init\(\)' 'Sleep wakeup path must restore the OLED display.'
Assert-Contains $sleepC 'Rs485_Service_SendRawText\(SLEEP_SERVICE_WAKEUP_TEXT\)' 'Wakeup response must be raw ASCII, not a protocol frame.'
Assert-Contains $sleepC 'RTC_Alarm_IRQHandler' 'Sleep service must document or implement the RTC alarm interrupt ownership.'
Assert-Contains $oledDriverC 'void OLED_DisPlay_Off\(void\)[\s\S]*OLED_WR_Byte\(0xAE,\s*OLED_CMD\)' 'OLED off command must send 0xAE, not 0xAF.'

Assert-Contains $rs485C '#include "Sleep_service.h"' 'RS485 service must include Sleep_service.'
Assert-Contains $rs485C 'case 0x03AAU:' 'RS485 service must handle command 0x03AA.'
Assert-Contains $rs485C 'Rs485_Service_SendOk\(frame->command\)' '0x03AA must send OK before sleeping.'
Assert-Contains $rs485C 'Sleep_Service_RequestRtcWakeup\(\)' '0x03AA must request RTC wakeup.'

Assert-Contains $functionC 'Sleep_Service_Init' 'System init must initialize sleep service.'
Assert-Contains $functionC 'Sleep_Service_Task' 'Main scheduler must run sleep task.'
Assert-Contains $functionH '#include "Sleep_service.h"' 'Function header must include Sleep_service.'
Assert-Contains $headerH '#include "Sleep_service.h"' 'HeaderFiles must include Sleep_service.'
Assert-Contains $itC 'RTC_Alarm_IRQHandler' 'Interrupt file must implement RTC_Alarm_IRQHandler.'
Assert-Contains $itC 'Sleep_Service_RtcAlarmIrqHandler\(\)' 'RTC alarm ISR must delegate to sleep service.'
Assert-Contains $rs485DmaC 'usart_flag_clear\(RS485_USART,\s*USART_FLAG_TC\)' 'RS485 DMA send must clear stale TC before starting a new DMA transfer.'
Assert-Contains $rs485DmaC 'while \(usart_flag_get\(RS485_USART,\s*USART_FLAG_TC\) == RESET\)' 'RS485 DMA send must wait for a fresh TC before lowering DE.'
Assert-Contains $uvproj 'Sleep_service\.c' 'Keil project must include Sleep_service.c.'
Assert-Contains $uvproj 'Sleep_service\.h' 'Keil project must include Sleep_service.h.'

Write-Host 'T09 sleep/wakeup static verification passed.'
