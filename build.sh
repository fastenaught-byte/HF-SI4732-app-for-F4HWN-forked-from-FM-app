#!/usr/bin/env bash
set -euo pipefail

APP_NAME="HF Receiver"
APP_VER="1.3.2"
APP_API_MIN=3
APP_VMA="${APP_VMA:-0x20000280}"
CC="${CC:-arm-none-eabi-gcc}"
OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"
SIZE="${SIZE:-arm-none-eabi-size}"
PYTHON="${PYTHON:-python3}"

APP_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$APP_DIR/build"
mkdir -p "$BUILD_DIR"

if [[ -n "${F4HWN_ROOT:-}" ]]; then
    if [[ ! -d "$F4HWN_ROOT" ]]; then
        echo "ERROR: F4HWN_ROOT directory not found: $F4HWN_ROOT" >&2
        exit 1
    fi
    ROOT_DIR="$(cd "$F4HWN_ROOT" && pwd)"
    PACK_APP="$ROOT_DIR/App/apps/pack_app.py"
    if [[ ! -f "$PACK_APP" ]]; then
        echo "ERROR: firmware packer not found: $PACK_APP" >&2
        exit 1
    fi
else
    ROOT_DIR=""
fi

echo "==> Building $APP_NAME v$APP_VER"
$CC -mcpu=cortex-m0plus -mthumb \
    -Os -flto -ffunction-sections -fdata-sections \
    -ffreestanding -nostdlib -Wall -Wextra -DSI4732_AM_ONLY \
    -I"$APP_DIR" -T"$APP_DIR/app.ld" \
    -Wl,--gc-sections -Wl,--undefined=memcpy -Wl,--undefined=memset \
    "-Wl,--defsym,APP_VMA=$APP_VMA" \
    -o "$BUILD_DIR/hf.elf" \
    "$APP_DIR/hf_app.c" "$APP_DIR/si4732_minimal.c" -lgcc
$OBJCOPY -O binary "$BUILD_DIR/hf.elf" "$BUILD_DIR/hf.bin"

echo "==> Building HF SSB v$APP_VER"
$CC -mcpu=cortex-m0plus -mthumb \
    -Os -ffunction-sections -fdata-sections \
    -ffreestanding -nostdlib -Wall -Wextra \
    -I"$APP_DIR" -T"$APP_DIR/app.ld" \
    -Wl,--gc-sections "-Wl,--defsym,APP_VMA=$APP_VMA" \
    -o "$BUILD_DIR/hf_ssb.elf" \
    "$APP_DIR/hf_ssb_app.c" "$APP_DIR/si4732_minimal.c" -lgcc
$OBJCOPY -O binary "$BUILD_DIR/hf_ssb.elf" "$BUILD_DIR/HFSSB.bin"

echo "==> Building SSB patch carrier stub"
$CC -mcpu=cortex-m0plus -mthumb \
    -Os -ffunction-sections -fdata-sections \
    -ffreestanding -nostdlib -Wall -Wextra \
    -I"$APP_DIR" -T"$APP_DIR/app.ld" \
    -Wl,--gc-sections "-Wl,--defsym,APP_VMA=$APP_VMA" \
    -o "$BUILD_DIR/ssb_patch_carrier.elf" \
    "$APP_DIR/ssb_patch_carrier.c" -lgcc
$OBJCOPY -O binary "$BUILD_DIR/ssb_patch_carrier.elf" "$BUILD_DIR/ssb_patch_carrier.bin"

echo
echo "==> Code sizes:"
$SIZE "$BUILD_DIR/hf.elf" "$BUILD_DIR/hf_ssb.elf" "$BUILD_DIR/ssb_patch_carrier.elf"
echo

if [[ -z "$ROOT_DIR" ]]; then
    echo "WARNING: F4HWN_ROOT not set; only .bin files were created."
    echo "         Set it to the firmware checkout to create installable .app files."
else
    "$PYTHON" "$PACK_APP" \
        "$BUILD_DIR/hf.bin" "$BUILD_DIR/HFReceiver.app" \
        --name "$APP_NAME" --ver "$APP_VER" --api-min "$APP_API_MIN" --vma "$APP_VMA"
    "$PYTHON" "$PACK_APP" \
        "$BUILD_DIR/HFSSB.bin" "$BUILD_DIR/HFSSB.app" \
        --name "HF SSB" --ver "$APP_VER" --api-min "$APP_API_MIN" \
        --vma "$APP_VMA" --require sysinfo
    "$PYTHON" "$APP_DIR/package_ssb_patch.py" \
        --patch "$APP_DIR/ssb_patches/amrx_6_0_1_ssbrx_patch_full_0x9D29.csg" \
        --packer "$PACK_APP" --stub-bin "$BUILD_DIR/ssb_patch_carrier.bin" \
        --output-dir "$BUILD_DIR" --ver "$APP_VER" --vma "$APP_VMA"
    echo "==> Created HFReceiver.app, HFSSB.app, and SSBPATCH0.app through SSBPATCH4.app"
fi

echo "Done."
