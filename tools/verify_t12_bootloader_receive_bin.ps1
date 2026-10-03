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
$bootUsartC = Join-Path $root 'CIMC_BootLoader\Driver\USART\usart.c'
$bootParamH = Join-Path $root 'CIMC_BootLoader\common\bl_param.h'
$appParamH = Join-Path $root 'CIMC_App\common\bl_param.h'
# 任务拆分计划属于内部开发记录，公开仓库中可能不存在，缺失时跳过相关断言。
$planDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'CIMC*.md' -or $_.Name -like '*任务拆分*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$manualDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs\verification') -File |
    Where-Object { $_.Name -like 'RS485*.md' } |
    Select-Object -First 1 -ExpandProperty FullName

foreach ($file in @($bootCoreC, $bootUsartC, $bootParamH, $appParamH, $planDoc, $manualDoc) | Where-Object { $_ }) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $bootParamH 'BL_UPDATE_FLAG_DOWNLOADED\s+0xA5A55A5AUL' 'BootLoader must define a downloaded-but-not-installed state for 0x0502.'
Assert-Contains $appParamH 'BL_UPDATE_FLAG_DOWNLOADED\s+0xA5A55A5AUL' 'APP shared header must keep the same downloaded state value.'

Assert-Contains $bootUsartC 'BL_USART_RX_BUFFER_SIZE\s+512U' 'BootLoader USART RX buffer must hold at least one 256-byte official slice.'
Assert-Contains $bootUsartC 'uint16_t recv_len' 'BootLoader USART receive length must be uint16_t, not uint8_t.'
Assert-Contains $bootUsartC 'recv_len\s*<\s*BL_USART_RX_BUFFER_SIZE' 'BootLoader USART IRQ must bounds-check RX writes.'

Assert-Contains $bootCoreC 'BL_OFFICIAL_BIN_MAGIC_SIZE\s+4U' 'BootLoader must treat the official bin magic as a 4-byte prefix.'
Assert-Contains $bootCoreC 'BL_OFFICIAL_BIN_RAM_BUFFER_SIZE\s+16384U' 'BootLoader must stage the small official contest bin in RAM before flash programming.'
Assert-Contains $bootCoreC '0x5AU' 'BootLoader must check official bin magic byte 0.'
Assert-Contains $bootCoreC '0xA5U' 'BootLoader must check official bin magic byte 1.'
Assert-Contains $bootCoreC '0xC3U' 'BootLoader must check official bin magic byte 2.'
Assert-Contains $bootCoreC '0x3CU' 'BootLoader must check official bin magic byte 3.'
Assert-Contains $bootCoreC 'bl_receive_official_firmware' 'BootLoader must have a raw official-bin receive routine.'
Assert-Contains $bootCoreC 'bl_append_official_payload_to_ram' 'BootLoader must avoid flash erase/program while official slices are still arriving.'
Assert-Contains $bootCoreC 'BL_ASCII_EMPTY_COMMAND_FRAME_LEN\s+\(CIMC_FRAME_FIXED_SIZE \* 2U\)' 'BootLoader must parse the fixed 26-byte ASCII command prefix even when raw bin bytes are glued behind 0x0502.'
Assert-Contains $bootCoreC 'static uint8_t bl_rx_ascii_buf\[BL_RAW_RX_BUFFER_SIZE\]' 'BootLoader command RX buffer must be large enough to preserve a glued 0x0502 command plus one official raw slice.'
Assert-Contains $bootCoreC 'bl_stage_glued_raw_bytes' 'BootLoader must stage raw firmware bytes that arrive in the same IDLE frame as 0x0502.'
Assert-Contains $bootCoreC 's_glued_raw_pending' 'BootLoader must remember that the first raw firmware bytes are already staged.'
Assert-Contains $bootCoreC 'bl_consume_glued_raw_bytes' 'BootLoader receive loop must consume staged raw bytes before waiting for the next USART frame.'
Assert-Contains $bootCoreC 'if\(!bl_consume_glued_raw_bytes\(\)\)\s*\{[\s\S]*return false;\s*\}' 'BootLoader firmware receiver must consume glued raw bytes immediately after resetting receive state.'
Assert-Contains $bootCoreC 'if\(\(s_download_payload_size > 0UL\) \|\| \(s_download_magic_index > 0U\)\)\s*\{\s*received_any = true;' 'BootLoader firmware receiver must treat a consumed glued prefix as received data for idle-timeout accounting.'
Assert-Contains $bootCoreC 'ascii_command_len\s*=\s*BL_ASCII_EMPTY_COMMAND_FRAME_LEN' 'BootLoader command parser must decode only the ASCII command prefix, not the following raw bin bytes.'
Assert-NotContains $bootCoreC 'if\(ascii_len > 255U\)\s*\{\s*return false;\s*\}' 'BootLoader must not reject glued command/raw frames solely because they exceed 255 bytes.'
Assert-NotContains $bootCoreC 'uint8_t normalized_len\s*=\s*\(uint8_t\)ascii_len' 'BootLoader must not truncate a glued command/raw receive length to uint8_t before staging the raw suffix.'
Assert-Contains $bootCoreC 'memcpy\(&bl_official_payload_buf\[s_download_payload_size\]' 'BootLoader must stage stripped payload bytes in RAM before flash programming.'
Assert-Contains $bootCoreC 'memset\(&bl_official_payload_buf\[s_download_payload_size\],\s*0xFF,\s*erase_size - s_download_payload_size\)' 'BootLoader must pad the final erased page before writing the download slot.'
Assert-Contains $bootCoreC 'bl_flash_program_bytes\(BL_DOWNLOAD_START_ADDR,\s*bl_official_payload_buf,\s*erase_size\)' 'BootLoader must write the page-aligned payload window to the download slot after the stream is idle.'
Assert-Contains $bootCoreC 'CimcProtocol_Crc32Update\(s_download_crc_acc' 'BootLoader must calculate CRC32 over the stored payload.'
Assert-Contains $bootCoreC 'working_param\.update_flag\s*=\s*BL_UPDATE_FLAG_DOWNLOADED' '0x0502 success must mark downloaded, not pending install.'
Assert-Contains $bootCoreC 'working_param\.app_size\s*=\s*s_download_payload_size' '0x0502 success must persist payload size without the magic prefix.'
Assert-Contains $bootCoreC 'working_param\.app_crc32\s*=\s*CimcProtocol_Crc32Finalize\(s_download_crc_acc\)' '0x0502 success must persist payload CRC for T13 validation.'
Assert-Contains $bootCoreC 'bl_send_error_frame\(0x0502U\)' '0x0502 receive failure must reply with the official error frame.'
Assert-Contains $bootCoreC 'bl_recover_after_rejected_firmware' '0x0502 receive failure must clean transient download metadata before the next attempt.'
Assert-Contains $bootCoreC 'param->update_flag\s*=\s*BL_UPDATE_FLAG_FAST_UPGRADE' 'Rejected firmware must keep the BootLoader upgrade session alive so the scorer can immediately run N-03.'
Assert-Contains $bootCoreC 'while\(wait_result\s*==\s*BL_UPGRADE_WAIT_RECEIVED_ERROR\)' 'BootLoader must continue waiting for another 0x0502 after reporting the N-02 bad image.'
Assert-Contains $bootCoreC 'bl_wait_for_upgrade_command\(false\)' 'The retry wait after N-02 must be silent so countdown text does not pollute the N-03 receive window.'
Assert-NotContains $bootCoreC 'bl_send_error_frame\(0x0502U\);\s*delay_1ms\(300U\);\s*NVIC_SystemReset\(\)' 'Rejected firmware must not reset back to APP before the official scorer sends N-03.'
Assert-NotContains $bootCoreC 'while\(1\)\s*\{\s*delay_1ms\(1000U\);\s*\}' 'BootLoader must not stay forever after rejecting an invalid official bin.'
Assert-Contains $bootCoreC 'bl_send_ok_frame\(0x0502U\)' '0x0502 receive success must reply with the official OK frame after receive.'
Assert-NotContains $bootCoreC 'if\(bl_is_prepare_transfer_frame\(\)\)\s*\{\s*bl_send_ok_frame\(0x0502U\);\s*return true;\s*\}' 'BootLoader must not acknowledge 0x0502 before receiving and checking the raw bin.'

if ($planDoc) { Assert-Contains $planDoc '### T12' 'Main plan must contain the T12 section.' }
if ($planDoc) { Assert-Contains $planDoc 'tools\\verify_t12_bootloader_receive_bin\.ps1' 'Main plan must document the T12 static verification command.' }
Assert-Contains $manualDoc 'T12' 'Manual checklist must include T12 human verification.'
Assert-Contains $manualDoc 'A5B600010105020002255AB6A5' 'Manual checklist must include the 0x0502 request frame.'

Write-Host 'T12 BootLoader official-bin receive static verification passed.'
