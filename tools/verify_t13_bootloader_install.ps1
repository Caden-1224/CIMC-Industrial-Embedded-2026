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
$bootPartitionH = Join-Path $root 'CIMC_BootLoader\common\bl_partition.h'
$bootParamH = Join-Path $root 'CIMC_BootLoader\common\bl_param.h'
# 任务拆分计划属于内部开发记录，公开仓库中可能不存在，缺失时跳过相关断言。
$planDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs') -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'CIMC*.md' -or $_.Name -like '*任务拆分*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$manualDoc = Get-ChildItem -LiteralPath (Join-Path $root 'docs\verification') -File |
    Where-Object { $_.Name -like 'RS485*.md' } |
    Select-Object -First 1 -ExpandProperty FullName
$memoryDoc = @((Join-Path $root 'PROJECT_MEMORY.md'),
    (Join-Path $root 'docs\dev-notes\PROJECT_MEMORY.md')) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

foreach ($file in @($bootCoreC, $bootPartitionH, $bootParamH, $planDoc, $manualDoc, $memoryDoc) | Where-Object { $_ }) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Missing required file: $file"
    }
}

Assert-Contains $bootPartitionH 'BL_APP_BACKUP_START_ADDR\s+0x08031000UL' 'T13 requires a dedicated APP rollback slot.'
Assert-Contains $bootPartitionH 'BL_DOWNLOAD_START_ADDR\s+0x08051000UL' 'T13 requires a dedicated downloaded-firmware slot.'
Assert-Contains $bootParamH 'BL_UPDATE_FLAG_DOWNLOADED\s+0xA5A55A5AUL' '0x0503 must consume the downloaded state persisted by T12.'
Assert-Contains $bootParamH 'BL_UPDATE_FLAG_FAILED\s+0xDEAD0001UL' 'T13 failures must persist an observable failed state.'

Assert-Contains $bootCoreC 'static bool bl_is_execute_upgrade_frame\(void\)' 'BootLoader must parse the official 0x0503 execute-upgrade command.'
Assert-Contains $bootCoreC 'bl_is_uart_command_frame\(0x0503U\)' 'The execute-upgrade parser must reject commands other than 0x0503.'
Assert-Contains $bootCoreC 'bl_wait_for_execute_upgrade_command' 'BootLoader must wait for 0x0503 after a downloaded image is ready.'
Assert-Contains $bootCoreC 'BL_EXECUTE_UPGRADE_WAIT_TIMEOUT_MS' 'The 0x0503 wait window must be bounded.'
Assert-Contains $bootCoreC 'bl_send_ok_frame\(0x0503U\)' '0x0503 must return OK before destructive flash copy starts.'
Assert-Contains $bootCoreC 'bl_send_error_frame\(0x0503U\)' 'Invalid or timed-out 0x0503 install paths must be observable over RS485.'
Assert-Contains $bootCoreC 'memset\(&bl_official_payload_buf\[s_download_payload_size\],\s*0xFF,\s*erase_size - s_download_payload_size\)' 'T12 must pad the staged official image to the erased page window before programming the download slot.'
Assert-Contains $bootCoreC 'bl_flash_program_bytes\(BL_DOWNLOAD_START_ADDR,\s*bl_official_payload_buf,\s*erase_size\)' 'T12 must program the complete page-aligned image window into the download slot.'

Assert-Contains $bootCoreC 'static bool bl_install_downloaded_image\(bl_param_t \*param, bl_log_entry_t \*log_entry\)' 'T13 install flow must be isolated from command parsing.'
Assert-Contains $bootCoreC 'bl_align_up_to_flash_page\(param->app_size\)' 'T13 must calculate the page-aligned install window.'
Assert-Contains $bootCoreC 'bl_backup_app1\(erase_size\)' 'T13 must back up only the APP1 pages that installation will erase.'
Assert-Contains $bootCoreC 'bl_copy_download_to_app1\(erase_size\)' 'T13 must program the entire erased page window, otherwise bytes after app_size in the last page remain erased and the official image may not boot.'
Assert-Contains $bootCoreC 'bl_restore_app1_from_backup\(erase_size\)' 'T13 must restore only the APP1 pages that were backed up.'
Assert-NotContains $bootCoreC 'bl_backup_app1\(void\)' 'T13 must not do an unconditional full-slot APP1 backup.'
Assert-NotContains $bootCoreC 'BL_APP1_START_ADDR,\s*BL_APP1_SIZE,\s*BL_APP_BACKUP_START_ADDR,\s*BL_APP_BACKUP_SIZE,\s*BL_APP1_SIZE' 'T13 rollback must not rewrite the full APP1 slot for a small official image.'
Assert-Contains $bootCoreC 'bl_crc32_flash\(BL_DOWNLOAD_START_ADDR,\s*param->app_size\)' 'T13 must verify the downloaded slot CRC before install.'
Assert-Contains $bootCoreC 'bl_crc32_flash\(BL_APP1_START_ADDR,\s*param->app_size\)' 'T13 must verify APP1 CRC after install.'
Assert-Contains $bootCoreC 'param->update_flag\s*=\s*BL_UPDATE_FLAG_IDLE' 'Successful T13 install must clear the update flag.'
Assert-Contains $bootCoreC 'param->update_flag\s*=\s*BL_UPDATE_FLAG_FAILED' 'Failed T13 install must persist FAILED.'
Assert-Contains $bootCoreC 'BL_LOG_EVENT_UPDATE_OK' 'Successful T13 install must append an update-ok log.'
Assert-Contains $bootCoreC 'BL_LOG_EVENT_UPDATE_FAIL' 'Failed T13 install must append an update-fail log.'

Assert-Contains $bootCoreC 'if\(working_param\.update_flag\s*==\s*BL_UPDATE_FLAG_DOWNLOADED\)' 'BootLoader must handle T12 DOWNLOADED state directly.'
Assert-NotContains $bootCoreC 'working_param\.update_flag\s*=\s*BL_UPDATE_FLAG_PENDING' 'Official 0x0503 path must not depend on the old pending flag state.'
Assert-Contains $bootCoreC 'bl_send_ok_frame\(0x0503U\);\s*__disable_irq\(\);\s*install_ok\s*=\s*bl_install_downloaded_image\(&working_param,\s*&log_entry\)' '0x0503 ACK must be sent before installation starts, then install must run without USART/SysTick interrupts during internal flash operations.'
Assert-Contains $bootCoreC 'commit_ok\s*=\s*bl_commit_param_page\(&working_param,\s*&log_entry,\s*true\)' 'T13 must check whether the final parameter/log commit succeeded.'
Assert-Contains $bootCoreC 'if\(\(!install_ok\)\s*\|\|\s*\(!commit_ok\)\)' 'T13 must report failure when install or final parameter commit fails.'
Assert-Contains $bootCoreC 'NVIC_SystemReset\(\)' 'T13 must reset after committing install or failure status.'

Assert-Contains $bootCoreC 'SysTick->CTRL\s*=\s*0UL' 'APP jump must stop SysTick before leaving BootLoader.'
Assert-Contains $bootCoreC 'NVIC->ICER\[i\]\s*=\s*0xFFFFFFFFUL' 'APP jump must disable NVIC interrupt lines.'
Assert-Contains $bootCoreC 'NVIC->ICPR\[i\]\s*=\s*0xFFFFFFFFUL' 'APP jump must clear pending NVIC interrupt lines.'
Assert-Contains $bootCoreC 'SCB->VTOR\s*=\s*app_base' 'APP jump must relocate VTOR to the application vector table.'
Assert-Contains $bootCoreC '__set_MSP\(\*\(volatile uint32_t \*\)app_base\)' 'APP jump must load the application MSP.'

if ($planDoc) { Assert-Contains $planDoc 'tools\\verify_t13_bootloader_install\.ps1' 'Main plan must document the T13 static verification command.' }
Assert-Contains $manualDoc 'T13' 'Manual checklist must include T13 human verification.'
Assert-Contains $manualDoc '0x0503' 'Manual checklist must include the 0x0503 execute-upgrade step.'
if ($memoryDoc) { Assert-Contains $memoryDoc 'T13 Bootloader' 'PROJECT_MEMORY must record the T13 architecture decision.' }

Write-Host 'T13 BootLoader install-and-rollback static verification passed.'
