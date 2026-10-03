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
$bootUsartC = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.c'
$bootUsartH = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.h'

foreach ($file in @($bootCoreC, $bootUsartC, $bootUsartH)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $bootUsartC 'BL_USART_RX_RING_SIZE\s+8192U' 'BootLoader USART RX must have enough byte-stream buffering for a 6420-byte official bin.'
Assert-Contains $bootUsartC 'static volatile uint16_t s_rx_ring_head' 'BootLoader USART RX ring must track an interrupt-written head index.'
Assert-Contains $bootUsartC 'static volatile uint16_t s_rx_ring_tail' 'BootLoader USART RX ring must track a foreground-read tail index.'
Assert-Contains $bootUsartC 'static void bl_usart_rx_ring_push' 'BootLoader USART IRQ must push every RBNE byte into a stream ring.'
Assert-Contains $bootUsartC 'bl_usart_read_byte\(uint8_t \*byte\)' 'BootLoader must expose byte-wise RX reads for continuous raw firmware streams.'
Assert-Contains $bootUsartC 'bl_usart_clear_rx_stream\(void\)' 'BootLoader must expose a way to clear stale bytes when entering OTA receive.'
Assert-Contains $bootUsartC 'rx_byte\s*=\s*\(uint8_t\)usart_data_receive\(USART\)' 'USART1 IRQ must read each RBNE byte exactly once.'
Assert-Contains $bootUsartC 'bl_usart_rx_ring_push\(rx_byte\)' 'USART1 IRQ must feed raw bytes directly into the stream ring.'

Assert-Contains $bootUsartH 'bool bl_usart_read_byte\(uint8_t \*byte\)' 'USART header must declare byte-wise BootLoader RX reads.'
Assert-Contains $bootUsartH 'void bl_usart_clear_rx_stream\(void\)' 'USART header must declare BootLoader RX stream cleanup.'

Assert-Contains $bootCoreC 'bl_read_uart_byte\(uint8_t \*byte\)' 'BootLoader core must consume USART as a byte stream, not only IDLE frames.'
Assert-Contains $bootCoreC 'while\(bl_read_uart_byte\(&ch\)\)' 'BootLoader command parser must drain available stream bytes while searching for CIMC frame end.'
Assert-Contains $bootCoreC 'bl_read_uart_byte\(&ch\)' 'Official-bin receiver must read raw bytes byte-by-byte from the stream ring.'
Assert-Contains $bootCoreC 'bl_usart_clear_rx_stream\(\);' 'BootLoader must clear stale APP/command bytes when entering the N-series OTA window.'
Assert-Contains $bootCoreC 'remaining >= 6U' 'N-01 banner keywords must be repeated across the evaluator scan window.'

Write-Host 'N-series BootLoader byte-stream RX static verification passed.'
