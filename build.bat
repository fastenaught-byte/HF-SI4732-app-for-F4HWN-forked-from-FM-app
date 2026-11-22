@echo off
setlocal

set "APP_VER=1.3.2"
set "APP_API_MIN=3"
if not defined APP_VMA set "APP_VMA=0x20000280"

set "CC=arm-none-eabi-gcc"
set "OBJCOPY=arm-none-eabi-objcopy"
set "SIZE=arm-none-eabi-size"
set "PYTHON=python"
set "SCRIPT_DIR=%~dp0"
set "APP_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%APP_DIR%\build"
set "PACK_APP=%APP_DIR%\pack_local_app.py"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where %CC% >nul 2>&1
if errorlevel 1 (
    echo *** ERROR: %CC% not found in PATH ***
    goto :compile_failed
)

where %PYTHON% >nul 2>&1
if errorlevel 1 (
    echo *** ERROR: %PYTHON% not found in PATH ***
    goto :package_failed
)

if defined F4HWN_ROOT (
    set "PACK_APP=%F4HWN_ROOT%\App\apps\pack_app.py"
    if not exist "%F4HWN_ROOT%\App\apps\pack_app.py" (
        echo *** ERROR: Firmware packer not found under F4HWN_ROOT ***
        goto :package_failed
    )
)

echo ==^> Building HF Receiver v%APP_VER%
%CC% -mcpu=cortex-m0plus -mthumb -Os -flto -ffunction-sections -fdata-sections -ffreestanding -nostdlib -Wall -Wextra -DSI4732_AM_ONLY -I"%APP_DIR%" -T"%APP_DIR%\app.ld" -Wl,--gc-sections -Wl,--undefined=memcpy -Wl,--undefined=memset -Wl,--defsym,APP_VMA=%APP_VMA% -o "%BUILD_DIR%\hf.elf" "%APP_DIR%\hf_app.c" "%APP_DIR%\si4732_minimal.c" -lgcc
if errorlevel 1 goto :compile_failed

%OBJCOPY% -O binary "%BUILD_DIR%\hf.elf" "%BUILD_DIR%\hf.bin"
if errorlevel 1 goto :compile_failed

echo ==^> Building HF SSB v%APP_VER%
%CC% -mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections -ffreestanding -nostdlib -Wall -Wextra -I"%APP_DIR%" -T"%APP_DIR%\app.ld" -Wl,--gc-sections -Wl,--defsym,APP_VMA=%APP_VMA% -o "%BUILD_DIR%\hf_ssb.elf" "%APP_DIR%\hf_ssb_app.c" "%APP_DIR%\si4732_minimal.c" -lgcc
if errorlevel 1 goto :compile_failed

%OBJCOPY% -O binary "%BUILD_DIR%\hf_ssb.elf" "%BUILD_DIR%\HFSSB.bin"
if errorlevel 1 goto :compile_failed

echo ==^> Building SSB patch carrier stub
%CC% -mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections -ffreestanding -nostdlib -Wall -Wextra -I"%APP_DIR%" -T"%APP_DIR%\app.ld" -Wl,--gc-sections -Wl,--defsym,APP_VMA=%APP_VMA% -o "%BUILD_DIR%\ssb_patch_carrier.elf" "%APP_DIR%\ssb_patch_carrier.c" -lgcc
if errorlevel 1 goto :compile_failed

%OBJCOPY% -O binary "%BUILD_DIR%\ssb_patch_carrier.elf" "%BUILD_DIR%\ssb_patch_carrier.bin"
if errorlevel 1 goto :compile_failed

echo.
echo ==^> Code sizes
%SIZE% "%BUILD_DIR%\hf.elf" "%BUILD_DIR%\hf_ssb.elf" "%BUILD_DIR%\ssb_patch_carrier.elf"
echo.

echo ==^> Packaging HF Receiver
%PYTHON% "%PACK_APP%" "%BUILD_DIR%\hf.bin" "%BUILD_DIR%\HFReceiver.app" --name "HF Receiver" --ver "%APP_VER%" --api-min "%APP_API_MIN%" --vma "%APP_VMA%"
if errorlevel 1 goto :package_failed
echo ==^> Packaging HF SSB
%PYTHON% "%PACK_APP%" "%BUILD_DIR%\HFSSB.bin" "%BUILD_DIR%\HFSSB.app" --name "HF SSB" --ver "%APP_VER%" --api-min "%APP_API_MIN%" --vma "%APP_VMA%"
if errorlevel 1 goto :package_failed
%PYTHON% "%APP_DIR%\package_ssb_patch.py" --patch "%APP_DIR%\ssb_patches\amrx_6_0_1_ssbrx_patch_full_0x9D29.csg" --packer "%PACK_APP%" --stub-bin "%BUILD_DIR%\ssb_patch_carrier.bin" --output-dir "%BUILD_DIR%" --ver "%APP_VER%" --vma "%APP_VMA%"
if errorlevel 1 goto :package_failed

echo.
echo Created HFReceiver.app, HFSSB.app, and SSBPATCH0.app through SSBPATCH4.app in %BUILD_DIR%
goto :end

:compile_failed
echo.
echo *** Compile failed ***
goto :failed

:package_failed
echo.
echo *** Packaging failed ***
goto :failed

:failed
echo.
pause
exit /b 1

:end
echo.
pause
exit /b 0
