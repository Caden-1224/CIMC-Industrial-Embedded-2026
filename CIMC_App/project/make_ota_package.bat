@echo off
setlocal
cd /d "%~dp0"

set "PYTHON=python"
set "PACKER=..\Tools\ota_stream_pack.py"
set "CONFIG=..\common\ota_package_config.h"
set "APP_BIN=.\Objects\CIMC_App.bin"
set "OTA_BIN=.\Objects\CIMC_App_ota.bin"

if not exist "%PACKER%" (
    echo [OTA] packer not found: %PACKER%
    goto fail
)

if not exist "%APP_BIN%" (
    echo [OTA] App bin not found: %APP_BIN%
    echo [OTA] Build CIMC_App first.
    goto fail
)

echo [OTA] Packing OTA image...
%PYTHON% "%PACKER%" --config "%CONFIG%" --bin "%APP_BIN%" --out "%OTA_BIN%"
if errorlevel 1 goto fail

echo [OTA] Done: %OTA_BIN%
exit /b 0

:fail
echo [OTA] Failed.
exit /b 1
