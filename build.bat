@echo off
setlocal EnableDelayedExpansion

REM ============================================================
REM  HF Receiver Overlay App – Build Script for Windows
REM ============================================================

set APP_NAME=HF Receiver
set APP_VER=0.1.0
set APP_API_MIN=3

set CC=arm-none-eabi-gcc
set OBJCOPY=arm-none-eabi-objcopy
set SIZE=arm-none-eabi-size

set SCRIPT_DIR=%~dp0
set APP_DIR=%SCRIPT_DIR%
set BUILD_DIR=%APP_DIR%build

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo ==> Building %APP_NAME% v%APP_VER%
echo.

REM Prüfen ob der Compiler gefunden wird
where %CC% >nul 2>&1
if errorlevel 1 (
    echo *** FEHLER: %CC% nicht gefunden ***
    echo.
    echo Bitte arm-none-eabi-gcc installieren und in den PATH legen.
    echo Download z.B. von: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
    echo.
    goto :ende
)

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
    goto :ende
)

%OBJCOPY% -O binary "%BUILD_DIR%\hf.elf" "%BUILD_DIR%\hf.bin"

echo.
echo ==> Size:
%SIZE% "%BUILD_DIR%\hf.elf"
echo.
echo ==> Binary: %BUILD_DIR%\hf.bin
echo.
echo Hinweis: Fuer das fertige .app brauchst du pack_app.py aus dem F4HWN-Repo.
echo.

:ende
echo.
pause
