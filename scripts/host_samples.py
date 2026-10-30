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
    parser.add_argument("--include-sampler", action="store_true",
                        help="keep __cyg_profile_* instrumentation samples (excluded by default)")
    parser.add_argument("--chains", action="store_true",
                        help="print call chains for --leaf")
    parser.add_argument("--leaf", default="svcSendSyncRequest",
                        help="leaf symbol to attribute chains for (default: svcSendSyncRequest)")
    parser.add_argument("--depth", type=int, default=4, help="frames in a printed chain")
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
            if len(parts) < 2:
                continue
            frames = [int(value, 16) for value in parts[1:] if value != "0"]
            if frames:
                rows.append(frames)
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
    total = 0
    for frames in rows:
        name = symbol_at(frames[0] - base, symbols, starts) or f"?+0x{frames[0] - base:X}"
        if not args.include_sampler and "cyg_profile" in name:
            continue
        counts[name] += 1
        total += 1
    if total == 0:
        raise SystemExit("no non-sampler samples")
    print(f"samples {args.samples}")
    print(f"rows    {total}")
    print(f"base    0x{base:X} (ref 0x{ref:X}, {ref_symbol[1]} 0x{ref_symbol[0]:X})")
    for name, count in counts.most_common(args.top):
        print(f"{100.0 * count / total:6.2f}%  {count:6d}  {name}")

    if args.chains:
        chains = collections.Counter()
        for frames in rows:
            names = []
            for pc in frames:
                name = symbol_at(pc - base, symbols, starts) or f"?+0x{pc - base:X}"
                if "cyg_profile" in name:
                    continue
                if not names or names[-1] != name:
                    names.append(name)
            if names and args.leaf in names[0]:
                chains[" <- ".join(names[:args.depth])] += 1
        chain_total = sum(chains.values())
        print(f"chains (leaf contains {args.leaf}, {chain_total} samples):")
        for chain, count in chains.most_common(args.top):
            print(f"{100.0 * count / chain_total:6.2f}%  {count:6d}  {chain}")


if __name__ == "__main__":
    main()
