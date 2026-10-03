#!/usr/bin/env python3
import argparse
import binascii
import os
import re
import struct
import sys


DEFAULT_CONFIG = os.path.join(os.path.dirname(__file__), "..", "common", "ota_package_config.h")


def parse_c_integer(text):
    text = text.strip()
    text = re.sub(r"[uUlL]+$", "", text)
    return int(text, 0) & 0xFFFFFFFF


def load_config(path):
    config = {}
    pattern = re.compile(r"^\s*#define\s+(OTA_PACKAGE_[A-Za-z0-9_]+)\s+(.+?)\s*(?:/\*.*)?$")

    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            match = pattern.match(line)
            if not match:
                continue
            name, value = match.groups()
            config[name] = parse_c_integer(value)

    required = [
        "OTA_PACKAGE_MAGIC",
        "OTA_PACKAGE_HEADER_VERSION",
        "OTA_PACKAGE_APP_VERSION",
        "OTA_PACKAGE_IMAGE_TYPE_APP1",
        "OTA_PACKAGE_TARGET_ADDR",
        "OTA_PACKAGE_MAX_SIZE",
    ]
    missing = [name for name in required if name not in config]
    if missing:
        raise SystemExit("missing config define(s): %s" % ", ".join(missing))

    return config


def parse_u32(value):
    return int(value, 0) & 0xFFFFFFFF


def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def main():
    parser = argparse.ArgumentParser(description="Pack App.bin into a raw UART OTA image.")
    parser.add_argument("--bin", required=True, help="input raw App.bin")
    parser.add_argument("--out", required=True, help="output OTA package")
    parser.add_argument("--config", default=DEFAULT_CONFIG, help="shared OTA package config header")
    parser.add_argument("--version", type=parse_u32, help="override app version")
    parser.add_argument("--target-addr", type=parse_u32, help="override linked APP start address")
    parser.add_argument("--max-size", type=parse_u32, help="override download cache size limit")
    args = parser.parse_args()
    cfg = load_config(args.config)

    app_version = cfg["OTA_PACKAGE_APP_VERSION"] if args.version is None else args.version
    target_addr = cfg["OTA_PACKAGE_TARGET_ADDR"] if args.target_addr is None else args.target_addr
    max_size = cfg["OTA_PACKAGE_MAX_SIZE"] if args.max_size is None else args.max_size

    with open(args.bin, "rb") as src:
        image = src.read()

    app_size = len(image)
    if app_size == 0:
        raise SystemExit("input App.bin is empty")
    if app_size > max_size:
        raise SystemExit("App.bin too large: %u > %u bytes" % (app_size, max_size))

    stack, reset = struct.unpack_from("<II", image, 0)
    if stack < 0x20000000 or stack >= 0x20040000:
        raise SystemExit("invalid vector table: initial MSP 0x%08X is not SRAM" % stack)
    if (reset & 0xFF000000) != 0x08000000:
        raise SystemExit("invalid vector table: reset handler 0x%08X is not flash" % reset)

    header_without_crc = struct.pack(
        "<IHHIIIII",
        cfg["OTA_PACKAGE_MAGIC"],
        32,
        cfg["OTA_PACKAGE_HEADER_VERSION"],
        app_version,
        app_size,
        crc32(image),
        target_addr,
        cfg["OTA_PACKAGE_IMAGE_TYPE_APP1"],
    )
    header_crc = crc32(header_without_crc)
    header = header_without_crc + struct.pack("<I", header_crc)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "wb") as dst:
        dst.write(header)
        dst.write(image)

    print("OTA package: %s" % args.out)
    print("  version=0x%08X size=%u app_crc=0x%08X header_crc=0x%08X" %
          (app_version, app_size, crc32(image), header_crc))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("ota_stream_pack.py: %s" % exc, file=sys.stderr)
        sys.exit(1)
