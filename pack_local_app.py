#!/usr/bin/env python3
"""Package this project's overlay binaries when the firmware packer is absent."""

import argparse
import pathlib
import re
import struct
import zlib

HEADER_SIZE = 64
MAX_APP_SIZE = 4096
MAX_ASSET_SIZE = 3840
MAGIC = b"FAP1"
HEADER_VERSION = 1
COMMITTED_FLAG = 1


def api_define(name):
    header = pathlib.Path(__file__).with_name("app_api.h")
    pattern = re.compile(
        r"^\s*#define\s+" + re.escape(name) + r"\s+(0[xX][0-9a-fA-F]+|\d+)[uUlL]*\b"
    )
    with header.open("r", encoding="ascii") as source:
        for line in source:
            match = pattern.match(line)
            if match:
                return int(match.group(1), 0)
    raise ValueError(f"{header}: missing {name}")


def field(value, length):
    encoded = value.encode("ascii", "strict")[:length - 1]
    return encoded + bytes(length - len(encoded))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("infile", type=pathlib.Path)
    parser.add_argument("outfile", type=pathlib.Path)
    parser.add_argument("--name", default="app")
    parser.add_argument("--ver", default="1.3.2")
    parser.add_argument("--api-min", type=int, required=True)
    parser.add_argument("--vma", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--assets", type=pathlib.Path)
    args = parser.parse_args()

    try:
        api_level = api_define("APP_API_LEVEL")
        abi_major = api_define("APP_ABI_MAJOR")
        asset_api = api_define("APP_API_ASSETS")
        code = args.infile.read_bytes()
        assets = args.assets.read_bytes() if args.assets else b""
        if not code:
            raise ValueError("empty input binary")
        if not 1 <= args.api_min <= api_level:
            raise ValueError(f"--api-min must be between 1 and {api_level}")
        if assets and args.api_min < asset_api:
            raise ValueError(f"assets require API level {asset_api} or higher")
        if len(assets) > MAX_ASSET_SIZE:
            raise ValueError(f"assets exceed the {MAX_ASSET_SIZE}-byte asset area")
        if HEADER_SIZE + len(code) + len(assets) > MAX_APP_SIZE:
            raise ValueError(f"app exceeds the {MAX_APP_SIZE}-byte package limit")

        header = struct.pack(
            "<4sHBBIIHH16s16sIIHH",
            MAGIC,
            HEADER_VERSION,
            abi_major,
            args.api_min,
            len(code),
            zlib.crc32(code) & 0xFFFFFFFF,
            0,
            COMMITTED_FLAG,
            field(args.name, 16),
            field(args.ver, 16),
            args.vma,
            0,
            len(assets),
            (zlib.crc32(assets) & 0xFFFF) if assets else 0,
        )
        args.outfile.parent.mkdir(parents=True, exist_ok=True)
        args.outfile.write_bytes(header + code + assets)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        parser.exit(1, f"error: {error}\n")

    print(f"{args.outfile}: code={len(code)} B assets={len(assets)} B "
          f"-> app {len(header) + len(code) + len(assets)} B")


if __name__ == "__main__":
    main()
