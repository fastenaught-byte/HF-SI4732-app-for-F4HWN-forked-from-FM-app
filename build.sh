#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# HF Receiver Overlay App – Build Script for F4HWN
# ============================================================

APP_NAME="HF Receiver"
APP_VER="0.1.0"
APP_API_MIN=3
APP_VMA="${APP_VMA:-0x20000280}"

# Toolchain
CC="${CC:-arm-none-eabi-gcc}"
OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"
SIZE="${SIZE:-arm-none-eabi-size}"

# Paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR=""
if [[ -n "${F4HWN_ROOT:-}" ]]; then
    if [[ ! -d "$F4HWN_ROOT" ]]; then
        echo "ERROR: F4HWN_ROOT directory not found: $F4HWN_ROOT" >&2
        exit 1
    fi
    ROOT_DIR="$(cd "$F4HWN_ROOT" && pwd)"
fi
APP_DIR="$SCRIPT_DIR"

# Source files
SRCS=(
    "$APP_DIR/hf_app.c"
    "$APP_DIR/si4732_minimal.c"
)

# Output
BUILD_DIR="$APP_DIR/build"
ELF="$BUILD_DIR/hf.elf"
BIN="$BUILD_DIR/hf.bin"
APP_OUT="$BUILD_DIR/HFReceiver.app"

mkdir -p "$BUILD_DIR"

echo "==> Building $APP_NAME v$APP_VER"

# Compile + Link
$CC -mcpu=cortex-m0plus -mthumb \
    -Os -ffunction-sections -fdata-sections \
    -ffreestanding -nostdlib \
    -Wall -Wextra \
    -I"$APP_DIR" \
    -T"$APP_DIR/app.ld" \
    -Wl,--gc-sections \
    "-Wl,--defsym,APP_VMA=$APP_VMA" \
    -o "$ELF" \
    "${SRCS[@]}" \
    -lgcc

# Binary
$OBJCOPY -O binary "$ELF" "$BIN"

# Size info
echo ""
echo "==> Size:"
$SIZE "$ELF"
echo ""

# Pack .app (if pack_app.py exists in the firmware tree)
PACK_APP="$ROOT_DIR/App/apps/pack_app.py"
if [[ -n "$ROOT_DIR" && -f "$PACK_APP" ]]; then
    python3 "$PACK_APP" \
        "$BIN" \
        "$APP_OUT" \
        --name "$APP_NAME" \
        --ver "$APP_VER" \
        --api-min "$APP_API_MIN" \
        --vma "$APP_VMA"
    echo "==> Created: $APP_OUT"
else
    echo "WARNING: pack_app.py unavailable – only .bin created"
    echo "         $BIN"
fi

echo "Done."
