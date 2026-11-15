#!/usr/bin/env python3
"""Packs a directory as a Nintendo RomFS image and zstd-compresses it.

    pack_romfs.py <input dir> <output.romfs> [--level N] [--keep]

Prints the assets.json component fields (sha256/size of the compressed file,
image_sha256/image_size of the image). Requires build_romfs from devkitPro and
the zstd CLI on PATH.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--level", type=int, default=3)
    parser.add_argument("--keep", action="store_true", help="keep the uncompressed image")
    args = parser.parse_args()

    if not args.input.is_dir():
        sys.exit(f"{args.input} is not a directory")

    build_romfs = shutil.which("build_romfs")
    if not build_romfs:
        devkitpro = os.environ.get("DEVKITPRO", "/opt/devkitpro")
        candidate = Path(devkitpro) / "tools" / "bin" / "build_romfs"
        if candidate.is_file():
            build_romfs = str(candidate)
        else:
            sys.exit("build_romfs not found; install devkitPro switch-tools")

    zstd = shutil.which("zstd")
    if not zstd:
        sys.exit("zstd not found on PATH")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([build_romfs, str(args.input), str(args.output)], check=True)

    compressed = args.output.with_name(args.output.name + ".zst")
    subprocess.run([zstd, "-f", f"-{args.level}", str(args.output), "-o", str(compressed)], check=True)

    component = {
        "url": f"<host>/{compressed.name}",
        "sha256": sha256(compressed),
        "size": compressed.stat().st_size,
        "image_sha256": sha256(args.output),
        "image_size": args.output.stat().st_size,
    }

    print(f"image      {args.output}  {component['image_size']} bytes")
    print(f"compressed {compressed}  {component['size']} bytes")
    print(json.dumps(component, indent=2))

    if not args.keep:
        args.output.unlink()


if __name__ == "__main__":
    main()
