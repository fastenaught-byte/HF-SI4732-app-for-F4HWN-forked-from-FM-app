@echo off
setlocal EnableDelayedExpansion

REM ============================================================
REM  HF Receiver Overlay App – Build Script for Windows
REM ============================================================

set APP_NAME=HF Receiver
set APP_VER=0.1.0
set APP_API_MIN=3

REM Toolchain – passe den Pfad an deine Installation an
set CC=arm-none-eabi-gcc
set OBJCOPY=arm-none-eabi-objcopy
set SIZE=arm-none-eabi-size

REM Pfade
set SCRIPT_DIR=%~dp0
set APP_DIR=%SCRIPT_DIR%
set BUILD_DIR=%APP_DIR%build

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo ==> Building %APP_NAME% v%APP_VER%

%CC% -mcpu=cortex-m0plus -mthumb ^
    -Os -ffunction-sections -fdata-sections ^
    -ffreestanding -nostdlib ^
    -Wall -Wextra ^
    -I"%APP_DIR%" ^
    -I"%APP_DIR%..\.." ^
    -T"%APP_DIR%app.ld" ^
    -Wl,--gc-sections ^
    -o "%BUILD_DIR%\hf.elf" ^
    "%APP_DIR%hf_app.c" ^
    "%APP_DIR%si4732_minimal.c" ^
    -lgcc

if errorlevel 1 (
    echo.
    echo *** Compile failed ***
    exit /b 1
)

%OBJCOPY% -O binary "%BUILD_DIR%\hf.elf" "%BUILD_DIR%\hf.bin"

echo.
echo ==> Size:
%SIZE% "%BUILD_DIR%\hf.elf"
echo.

echo ==> Binary: %BUILD_DIR%\hf.bin
echo.
echo Hinweis: Fuer das fertige .app brauchst du pack_app.py aus dem F4HWN-Repo.
echo          Oder du baust die App innerhalb des offiziellen Firmware-Trees.
echo.
echo Done.
pause
