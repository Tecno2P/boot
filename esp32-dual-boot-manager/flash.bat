@echo off
setlocal EnableDelayedExpansion
title ESP32 Dual Boot Manager - Flash Tool

echo.
echo ============================================================
echo   ESP32 Dual Boot Manager - Windows Flash Script
echo ============================================================
echo.

REM ── Check for esptool.py ──────────────────────────────────────
where esptool.py >nul 2>&1
if errorlevel 1 (
    where esptool >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] esptool.py not found in PATH.
        echo         Install with: pip install esptool
        echo.
        pause
        exit /b 1
    ) else (
        set ESPTOOL=esptool
    )
) else (
    set ESPTOOL=esptool.py
)

echo [OK] Found esptool: %ESPTOOL%
echo.

REM ── Locate binary files ───────────────────────────────────────
set SKETCH_DIR=esp32-dual-boot-manager
set BUILD_DIR=%SKETCH_DIR%\build

REM Try to find pre-built binaries from Arduino IDE export
set FIRMWARE_BIN=
for /r "." %%f in (dual_boot_manager.ino.bin) do (
    set FIRMWARE_BIN=%%f
    goto :found_firmware
)
:found_firmware

if "%FIRMWARE_BIN%"=="" (
    echo [WARN] dual_boot_manager.ino.bin not found.
    echo        Please export compiled binary from Arduino IDE first:
    echo        Sketch ^> Export Compiled Binary
    echo.
)

REM ── Auto-detect COM port ──────────────────────────────────────
echo Detecting available COM ports...
set COM_PORT=
for /f "tokens=1,2*" %%a in ('reg query HKLM\HARDWARE\DEVICEMAP\SERIALCOMM 2^>nul') do (
    echo   Found: %%c
    if "!COM_PORT!"=="" set COM_PORT=%%c
)

if "%COM_PORT%"=="" (
    echo [WARN] No COM port auto-detected.
    set /p COM_PORT=Enter COM port manually (e.g. COM3):
)

echo.
echo [INFO] Using port: %COM_PORT%
echo [INFO] Baud rate: 921600
echo [INFO] Chip: ESP32
echo.

REM ── Confirm flash ─────────────────────────────────────────────
echo This will flash the following to %COM_PORT%:
echo   0x1000   - Bootloader
echo   0x8000   - Partition Table (partitions.csv)
echo   0xe000   - OTA Data (initial)
echo   0x10000  - Firmware (app0 slot)
echo   0x2A0000 - SPIFFS data (if present)
echo.
set /p CONFIRM=Proceed? (y/N):
if /i not "%CONFIRM%"=="y" (
    echo Cancelled.
    pause
    exit /b 0
)

REM ── Locate component binaries ─────────────────────────────────
set BOOTLOADER=
set PARTITIONS=
set OTADATA=
set SPIFFS_BIN=

REM Search for bootloader
for /r "." %%f in (bootloader.bin) do (
    if "!BOOTLOADER!"=="" set BOOTLOADER=%%f
)

REM Search for partitions binary
for /r "." %%f in (partitions.bin) do (
    if "!PARTITIONS!"=="" set PARTITIONS=%%f
)

REM Search for OTA data
for /r "." %%f in (boot_app0.bin) do (
    if "!OTADATA!"=="" set OTADATA=%%f
)

REM Search for SPIFFS
for /r "." %%f in (spiffs.bin) do (
    if "!SPIFFS_BIN!"=="" set SPIFFS_BIN=%%f
)

REM ── Build flash command ───────────────────────────────────────
set FLASH_CMD=%ESPTOOL% --chip esp32 --port %COM_PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB

if not "%BOOTLOADER%"=="" (
    set FLASH_CMD=!FLASH_CMD! 0x1000 "!BOOTLOADER!"
    echo [INFO] Bootloader: !BOOTLOADER!
) else (
    echo [WARN] Bootloader binary not found, skipping.
)

if not "%PARTITIONS%"=="" (
    set FLASH_CMD=!FLASH_CMD! 0x8000 "!PARTITIONS!"
    echo [INFO] Partitions: !PARTITIONS!
) else (
    echo [WARN] Partitions binary not found, skipping.
)

if not "%OTADATA%"=="" (
    set FLASH_CMD=!FLASH_CMD! 0xe000 "!OTADATA!"
    echo [INFO] OTA Data: !OTADATA!
) else (
    echo [WARN] OTA data binary not found, skipping.
)

if not "%FIRMWARE_BIN%"=="" (
    set FLASH_CMD=!FLASH_CMD! 0x10000 "!FIRMWARE_BIN!"
    echo [INFO] Firmware: !FIRMWARE_BIN!
) else (
    echo [WARN] Firmware binary not found, skipping.
)

if not "%SPIFFS_BIN%"=="" (
    set FLASH_CMD=!FLASH_CMD! 0x2A0000 "!SPIFFS_BIN!"
    echo [INFO] SPIFFS: !SPIFFS_BIN!
) else (
    echo [WARN] SPIFFS binary not found.
    echo        Generate with: Tools ^> ESP32 Sketch Data Upload (in Arduino IDE)
    echo        or: mkspiffs / mklittlefs tool
)

echo.
echo Flashing...
echo.

%FLASH_CMD%

if errorlevel 1 (
    echo.
    echo [ERROR] Flash failed! Check:
    echo   - Device is connected and in bootloader mode
    echo   - Correct COM port selected
    echo   - esptool.py is up to date: pip install --upgrade esptool
    echo   - Hold BOOT button while pressing RESET to enter download mode
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo   Flash complete! Device is rebooting...
echo   Connect to WiFi: ESP32-DualBoot
echo   Password:        admin1234
echo   Web GUI:         http://192.168.4.1
echo ============================================================
echo.
pause
exit /b 0
