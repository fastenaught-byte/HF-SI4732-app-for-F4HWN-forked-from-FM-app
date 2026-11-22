#!/usr/bin/env python3
"""Package a local SI4732 patch stream as private F4HWN app-slot assets."""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

PATCH_SIZE = 15832
PART_SIZE = 3832
RECORD_SIZE = 8
PART_COUNT = 5


def parse_patch(path):
    data = bytearray()
    token_pattern = re.compile(r"0[xX][0-9a-fA-F]{1,2}[uU]?\Z")
    with path.open("r", encoding="ascii") as patch_file:
        for line_number, line in enumerate(patch_file, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            tokens = [token.strip() for token in line.split(",")]
            if tokens and not tokens[-1]:
                tokens.pop()
            if len(tokens) != RECORD_SIZE:
                raise ValueError(f"{path}:{line_number}: expected 8 bytes per record")
            for token in tokens:
                if not token_pattern.fullmatch(token):
                    raise ValueError(f"{path}:{line_number}: invalid byte {token!r}")
                value = int(token.rstrip("uU"), 16)
                if value > 0xFF:
                    raise ValueError(f"{path}:{line_number}: byte out of range {token!r}")
                data.append(value)

    if len(data) != PATCH_SIZE:
        raise ValueError(f"{path}: expected {PATCH_SIZE} bytes, found {len(data)}")
    for offset in range(0, len(data), RECORD_SIZE):
        command = data[offset]
        if command not in (0x15, 0x16):
            raise ValueError(f"{path}: invalid command byte at offset {offset}")
        if offset == 0 and command != 0x15:
            raise ValueError(f"{path}: first record must begin with 0x15")
    return data


def package_parts(data, packer, stub_bin, output_dir, version, vma):
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ssb-assets-", dir=str(output_dir)) as temp:
        temp_dir = pathlib.Path(temp)
        for part in range(PART_COUNT):
            start = part * PART_SIZE
            chunk = data[start:start + PART_SIZE]
            asset_path = temp_dir / f"part{part}.bin"
            asset_path.write_bytes(chunk)
            output_path = output_dir / f"SSBPATCH{part}.app"
            command = [
                sys.executable, str(packer), str(stub_bin), str(output_path),
                "--name", f"SSBPATCH{part}", "--ver", version,
                "--api-min", "2", "--vma", vma, "--assets", str(asset_path),
            ]
            subprocess.run(command, check=True)
            if output_path.stat().st_size > 4096:
                raise ValueError(f"{output_path}: package exceeds the 4096-byte limit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--patch", required=True, type=pathlib.Path)
    parser.add_argument("--packer", required=True, type=pathlib.Path)
    parser.add_argument("--stub-bin", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--ver", default="1.3.2")
    parser.add_argument("--vma", default="0x20000280")
    args = parser.parse_args()

    if not args.packer.is_file():
        parser.error(f"packer not found: {args.packer}")
    if not args.stub_bin.is_file():
        parser.error(f"carrier stub binary not found: {args.stub_bin}")
    try:
        data = parse_patch(args.patch)
        package_parts(
            data, args.packer, args.stub_bin, args.output_dir, args.ver, args.vma
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
