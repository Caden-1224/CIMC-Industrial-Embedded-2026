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

$root = Split-Path -Parent $PSScriptRoot
$bootCoreC = Join-Path $root 'CIMC_BootLoader\Function\bl_core.c'
$bootUsartH = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.h'

foreach ($file in @($bootCoreC, $bootUsartH)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $bootUsartH '#define USART\s+USART1' 'BootLoader handoff guard assumes the competition RS485 port is USART1.'
Assert-Contains $bootUsartH '#define USART_IRQn\s+USART1_IRQn' 'BootLoader handoff guard must clear the USART1 NVIC line before jumping to APP.'
Assert-Contains $bootUsartH '#define RS485_DE_PORT\s+GPIOE' 'BootLoader handoff guard must know the RS485 DE port.'
Assert-Contains $bootUsartH '#define RS485_DE_PIN\s+GPIO_PIN_8' 'BootLoader handoff guard must know the RS485 DE pin.'

Assert-Contains $bootCoreC '#include "usart\.h"' 'bl_core.c must include the BootLoader USART contract before touching USART1 handoff state.'
Assert-Contains $bootCoreC 'static void bl_release_uart_before_app_jump\(void\)' 'BootLoader must isolate USART1/RS485 cleanup in one handoff helper.'
Assert-Contains $bootCoreC 'usart_interrupt_disable\(USART,\s*USART_INT_RBNE\)' 'BootLoader must disable USART1 byte receive interrupts before APP jump.'
Assert-Contains $bootCoreC 'usart_interrupt_disable\(USART,\s*USART_INT_IDLE\)' 'BootLoader must disable USART1 IDLE interrupts before APP jump.'
Assert-Contains $bootCoreC 'NVIC_DisableIRQ\(USART_IRQn\)' 'BootLoader must disable the USART1 NVIC line before APP jump.'
Assert-Contains $bootCoreC 'NVIC_ClearPendingIRQ\(USART_IRQn\)' 'BootLoader must clear any pending USART1 IRQ before APP jump.'
Assert-Contains $bootCoreC 'usart_receive_config\(USART,\s*USART_RECEIVE_DISABLE\)' 'BootLoader must turn off the USART1 receiver before APP jump.'
Assert-Contains $bootCoreC 'usart_transmit_config\(USART,\s*USART_TRANSMIT_DISABLE\)' 'BootLoader must turn off the USART1 transmitter before APP jump.'
Assert-Contains $bootCoreC 'usart_disable\(USART\)' 'BootLoader must disable USART1 itself before APP reinitializes the port.'
Assert-Contains $bootCoreC 'gpio_bit_reset\(RS485_DE_PORT,\s*RS485_DE_PIN\)' 'BootLoader must release RS485 DE before APP starts sending heartbeats.'
Assert-Contains $bootCoreC 'bl_release_uart_before_app_jump\(\);\s*__disable_irq\(\);' 'USART1 cleanup must run immediately before global IRQ shutdown in bl_jump_to_app.'

Write-Host 'N-03 BootLoader APP handoff static verification passed.'
