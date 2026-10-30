#!/usr/bin/env python3
"""Summarize NSMBW_SAMPLE_HOST host-PC samples against the unstripped ELF."""

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
        if len(parts) != 4:
            continue
        address, size, kind, name = parts
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
            end = symbols[index + 1][0] if index + 1 < len(symbols) else address + 1
            symbols[index] = (address, max(1, end - address), name)
    return symbols, [entry[0] for entry in symbols]


def symbol_at(address, symbols, starts):
    index = bisect.bisect_right(starts, address) - 1
    if index >= 0:
        base, size, name = symbols[index]
        if base <= address < base + size:
            return name
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("samples", help="host-samples.tsv from the run log directory")
    parser.add_argument("--elf", required=True, help="unstripped ELF that produced the samples")
    parser.add_argument("--top", type=int, default=30)
    args = parser.parse_args()

    symbols, starts = load_symbols(args.elf)
    ref = None
    rows = []
    with Path(args.samples).open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("#"):
                parts = line.split()
                if len(parts) >= 3 and parts[1] == "ref":
                    ref = int(parts[2], 16)
                continue
            if line.startswith("tick_us"):
                continue
            parts = line.split()
            if len(parts) != 2:
                continue
            rows.append(int(parts[1], 16))
    if ref is None:
        raise SystemExit("missing '# ref <hex>' header")
    if not rows:
        raise SystemExit("no samples")

    ref_symbol = next(((address, name) for address, _, name in symbols
                       if "NsmbwShadowSamplerInit" in name), None)
    if ref_symbol is None:
        raise SystemExit("NsmbwShadowSamplerInit not found in the ELF")
    base = ref - ref_symbol[0]
    if base < 0:
        raise SystemExit(f"invalid base 0x{base:X} (ref below symbol)")

    counts = collections.Counter()
    for pc in rows:
        name = symbol_at(pc - base, symbols, starts)
        counts[name or f"?+0x{pc - base:X}"] += 1
    total = len(rows)
    print(f"samples {args.samples}")
    print(f"rows    {total}")
    print(f"base    0x{base:X} (ref 0x{ref:X}, {ref_symbol[1]} 0x{ref_symbol[0]:X})")
    for name, count in counts.most_common(args.top):
        print(f"{100.0 * count / total:6.2f}%  {count:6d}  {name}")


if __name__ == "__main__":
    main()
