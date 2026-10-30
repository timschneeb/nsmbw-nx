#!/usr/bin/env python3
"""Summarize shadow-samples.tsv by re-symbolizing the raw host PC against the ELF."""

import argparse
import bisect
import collections
import subprocess
from pathlib import Path


def load_symbols(path):
    output = subprocess.check_output(["nm", "-n", "-S", "--defined-only", path], text=True,
                                     errors="replace")
    symbols = []
    for line in output.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 3:
            address, kind, name = parts
            size = "0"
        elif len(parts) == 4:
            address, size, kind, name = parts
        else:
            continue
        if kind.lower() not in ("t", "w"):
            continue
        try:
            address = int(address, 16)
            size = int(size, 16)
        except ValueError:
            continue
        symbols.append((address, size, name))
    symbols.sort()
    for index, (address, size, name) in enumerate(symbols):
        if size == 0:
            end = symbols[index + 1][0] if index + 1 < len(symbols) else address + 4
            symbols[index] = (address, max(1, end - address), name)
    return symbols, [entry[0] for entry in symbols]


def symbol_at(address, symbols, starts):
    index = bisect.bisect_right(starts, address) - 1
    if index >= 0:
        base, size, name = symbols[index]
        if base <= address < base + size:
            return name
    return None


def load_map(path):
    names = {}
    if not Path(path).is_file():
        return names
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and len(parts[0]) == 8:
            try:
                names[int(parts[0], 16)] = parts[1].strip()
            except ValueError:
                continue
    return names


def guest_address(name):
    if name and name.startswith("func_") and len(name) == 13:
        try:
            return int(name[5:], 16)
        except ValueError:
            return None
    return None


def category(name):
    if name is None:
        return "unknown"
    if any(key in name for key in ("nw4r3snd", "AxVoice", "AxManager", "Axfx")) or \
            name.startswith("__AX"):
        return "audio"
    if "g3d" in name or name.startswith("m3d"):
        return "NW4R g3d"
    if "EGG" in name:
        return "EGG"
    if name.startswith("GX"):
        return "GX"
    if name.startswith("func_") or name[:1] == "0":
        return "unnamed guest"
    return "guest game code"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("samples")
    parser.add_argument("--elf", required=True)
    parser.add_argument("--map", default="projects/nsmbw/MAP.txt")
    parser.add_argument("--scene")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--categories", action="store_true")
    args = parser.parse_args()

    symbols, starts = load_symbols(args.elf)
    ref_symbol = next((address for address, _, name in symbols
                       if "NsmbwShadowSamplerInit" in name), None)
    if ref_symbol is None:
        raise SystemExit("NsmbwShadowSamplerInit not found in the ELF")
    names = load_map(args.map)

    ref = None
    rows = []
    with Path(args.samples).open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("#"):
                parts = line.split()
                if len(parts) >= 3 and parts[1] == "ref":
                    ref = int(parts[2], 16)
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 6 or fields[0] == "scene":
                continue
            if args.scene and fields[0] != args.scene:
                continue
            try:
                host = int(fields[4], 16)
            except ValueError:
                continue
            rows.append(host)
    if ref is None:
        raise SystemExit("missing '# ref <hex>' header")
    base = ref - ref_symbol

    counts = collections.Counter()
    by_category = collections.Counter()
    unresolved = 0
    for host in rows:
        symbol = symbol_at(host - base, symbols, starts)
        if symbol is None:
            unresolved += 1
            continue
        address = guest_address(symbol)
        name = names.get(address, symbol) if address is not None else symbol
        counts[name] += 1
        by_category[category(symbol)] += 1

    total = sum(counts.values())
    print(f"samples {Path(args.samples)}")
    print(f"rows    {len(rows)} resolved {total} unresolved {unresolved}")
    print(f"base    0x{base:X} (ref 0x{ref:X})")
    for name, count in counts.most_common(args.top):
        print(f"{100.0 * count / total:6.2f}%  {count:6d}  {name}")
    if args.categories:
        print()
        for name, count in by_category.most_common():
            print(f"{100.0 * count / total:6.2f}%  {count:6d}  {name}")


if __name__ == "__main__":
    main()
