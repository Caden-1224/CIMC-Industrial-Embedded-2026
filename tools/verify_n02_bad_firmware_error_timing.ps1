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

if (-not (Test-Path -LiteralPath $bootCoreC)) {
    throw "Missing required file: $bootCoreC"
}

Assert-Contains $bootCoreC 'BL_OFFICIAL_BIN_ERROR_QUIET_MS\s+100UL' `
    'N-02 bad firmware must wait for the raw stream to become quiet before replying ERROR.'
Assert-Contains $bootCoreC 'static void bl_drain_official_stream_until_quiet\(void\)' `
    'BootLoader must drain the rest of the bad raw firmware stream before sending 0x0502 ERROR.'
Assert-Contains $bootCoreC 'while\(quiet_ms < BL_OFFICIAL_BIN_ERROR_QUIET_MS\)' `
    'Bad-firmware drain must use a quiet-window condition, not a fixed blind delay.'
Assert-Contains $bootCoreC 'if\(!bl_append_official_payload_to_ram\(bl_raw_rx_buf, 1U\)\)\s*\{\s*bl_drain_official_stream_until_quiet\(\);\s*return false;' `
    'N-02 receiver must defer ERROR until after draining the bad firmware stream.'
Assert-Contains $bootCoreC 'if\(!bl_consume_glued_raw_bytes\(\)\)\s*\{\s*bl_drain_official_stream_until_quiet\(\);\s*return false;' `
    'N-02 receiver must also drain when the bad magic byte was staged behind the 0x0502 command.'

Write-Host 'N-02 bad-firmware ERROR timing static verification passed.'
