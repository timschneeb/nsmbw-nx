#!/usr/bin/env python3
"""Summarize GuestProfiler's per-thread sample trace."""

import argparse
import bisect
import collections
import csv
import json
import subprocess
from pathlib import Path


def load_symbols(path):
    output = subprocess.check_output(["nm", "-n", "-S", "--defined-only", path], text=True, errors="replace")
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
            size = max(1, (symbols[index + 1][0] if index + 1 < len(symbols) else address + 1) - address)
        symbols[index] = (address, size, name)
    return symbols, [entry[0] for entry in symbols]


def symbol_at(address, symbols, starts):
    index = bisect.bisect_right(starts, address) - 1
    if index >= 0:
        base, size, name = symbols[index]
        if base <= address < base + size:
            return name
    return None


def load(path, scene, elf, name_filter=None, guest_only=False, function_filter=None,
         exclude_sampler=True, exclude_foreign=False):
    symbols = starts = None
    module = None
    if elf:
        symbols, starts = load_symbols(str(elf))
        module = Path(elf).name
    counts = collections.Counter()
    threads = collections.Counter()
    cores = collections.Counter()
    guests = collections.Counter()
    stats = collections.Counter()
    rows = 0
    with Path(path).open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if scene and row["scene"] != scene:
                continue
            name = row.get("name", "?")
            if name_filter and name_filter not in name:
                continue
            if guest_only and row["guest"] == "0":
                continue
            function = row["function"]
            resolved_from_elf = False
            if symbols and module and "+" in row["pc"]:
                module_name, offset = row["pc"].rsplit("+", 1)
                if module_name == module:
                    try:
                        resolved = symbol_at(int(offset, 16), symbols, starts)
                    except ValueError:
                        resolved = None
                    if resolved:
                        function = resolved
                        resolved_from_elf = True
                    else:
                        stats["unresolved"] += 1
                else:
                    stats["foreign"] += 1
            elif symbols:
                stats["foreign"] += 1
            if exclude_sampler and function.startswith("__cyg_profile_"):
                stats["sampler"] += 1
                continue
            if exclude_foreign and symbols and not resolved_from_elf:
                continue
            if function_filter and function_filter not in function:
                continue
            rows += 1
            counts[function] += 1
            thread = f"{name} tid {row['tid']} core {row['core']} guest {row['guest']}"
            threads[thread] += 1
            cores[row["core"]] += 1
            guests[row["guest"]] += 1
    return rows, counts, threads, cores, guests, stats


def resolve_stack_frame(frame, symbols, starts):
    if "+" in frame:
        return frame, False
    try:
        address = int(frame, 16)
    except ValueError:
        return frame, False
    name = symbol_at(address, symbols, starts)
    return (name or f"?+0x{address:X}"), True


def load_blocked(path, scene, elf, exclude_sampler=True):
    symbols, starts = load_symbols(str(elf))
    chains = collections.Counter()
    leaves = collections.Counter()
    callers = collections.Counter()
    total = 0
    blocked = 0
    with Path(path).open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.split()
            if scene and parts[0] != scene:
                continue
            total += 1
            frames = parts[1:]
            if not frames:
                continue
            leaf, leaf_self = resolve_stack_frame(frames[0], symbols, starts)
            if leaf_self:
                continue
            if exclude_sampler and leaf.startswith("__cyg_profile_"):
                continue
            caller = "<unknown>"
            for frame in frames[1:]:
                name, is_self = resolve_stack_frame(frame, symbols, starts)
                if is_self and not name.startswith("__cyg_profile_"):
                    caller = name
                    break
            blocked += 1
            chains[f"{leaf} <- {caller}"] += 1
            leaves[leaf] += 1
            callers[caller] += 1
    return total, blocked, chains, leaves, callers


def top(counter, rows, limit):
    result = []
    for name, count in counter.most_common(limit):
        result.append({"name": name, "samples": count, "percent": 100.0 * count / rows})
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("samples", help="profile-samples.tsv from the run log directory")
    parser.add_argument("--scene", default="S005", help="scene tag to keep, e.g. S005 (default), or empty for all")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--by", choices=("function", "thread", "core", "guest"), default="function")
    parser.add_argument("--compare", help="baseline profile-samples.tsv")
    parser.add_argument("--elf", help="unstripped binary for runtime symbol names")
    parser.add_argument("--name", help="host thread name substring, e.g. ax-mix")
    parser.add_argument("--function", help="function name substring, e.g. AxfxDelay")
    parser.add_argument("--guest-only", action="store_true", help="drop samples from non-guest host threads")
    parser.add_argument("--include-sampler", action="store_true",
                        help="keep __cyg_profile_* instrumentation samples (excluded by default)")
    parser.add_argument("--exclude-foreign", action="store_true",
                        help="drop samples outside the ELF module (idle waits, libc, SDL)")
    parser.add_argument("--blocked", action="store_true",
                        help="attribute blocked (foreign-leaf) samples to the first guest frame "
                             "from profile-stacks.txt")
    parser.add_argument("--stacks", help="profile-stacks.txt (default: next to --samples)")
    parser.add_argument("--json")
    args = parser.parse_args()

    scene = args.scene or None
    if args.blocked:
        if not args.elf:
            raise SystemExit("--blocked requires --elf")
        stacks_path = Path(args.stacks) if args.stacks else Path(args.samples).with_name("profile-stacks.txt")
        if not stacks_path.is_file():
            raise SystemExit(f"missing stacks file: {stacks_path}")
        total, blocked, chains, leaves, callers = load_blocked(
            stacks_path, scene, args.elf, exclude_sampler=not args.include_sampler)
        if total == 0:
            raise SystemExit(f"no samples for scene {scene or 'all'} in {stacks_path}")
        print(f"stacks  {stacks_path}")
        print(f"rows    {total} ({scene or 'all scenes'})")
        print(f"blocked {blocked} ({100.0 * blocked / total:.1f}% of samples)")
        if blocked == 0:
            return
        print("blocked chains (share of blocked samples):")
        for chain, count in chains.most_common(args.top):
            print(f"{100.0 * count / blocked:6.2f}%  {count:6d}  {chain}")
        print("blocked leaves:")
        for leaf, count in leaves.most_common(min(args.top, 10)):
            print(f"{100.0 * count / blocked:6.2f}%  {count:6d}  {leaf}")
        print("guest callers:")
        for caller, count in callers.most_common(min(args.top, 10)):
            print(f"{100.0 * count / blocked:6.2f}%  {count:6d}  {caller}")
        return
    rows, counts, threads, cores, guests, stats = load(
        args.samples, scene, args.elf, args.name, args.guest_only, args.function,
        exclude_sampler=not args.include_sampler, exclude_foreign=args.exclude_foreign)
    if rows == 0:
        raise SystemExit(f"no samples for scene {scene or 'all'} in {args.samples}")

    counters = {"function": counts, "thread": threads, "core": cores, "guest": guests}
    result = {
        "samples": args.samples,
        "rows": rows,
        "scene": scene or "all",
        "by": args.by,
        "excluded": dict(stats),
        "top": top(counters[args.by], rows, args.top),
    }
    print(f"samples {args.samples}")
    print(f"rows    {rows} ({scene or 'all scenes'})")
    if stats:
        print("excluded " + ", ".join(f"{name} {value}" for name, value in sorted(stats.items())))
    for item in result["top"]:
        print(f"{item['percent']:6.2f}%  {item['samples']:8d}  {item['name']}")

    if args.compare:
        base_rows, base_counts, _, _, _, _ = load(
            args.compare, scene, args.elf, args.name, args.guest_only, args.function,
            exclude_sampler=not args.include_sampler, exclude_foreign=args.exclude_foreign)
        if base_rows:
            print(f"compare {args.compare} ({base_rows} samples)")
            keys = collections.Counter()
            for item in result["top"]:
                keys[item["name"]] = item["samples"]
            base_sorted = base_counts.most_common(args.top)
            for name, base_count in base_sorted:
                now = keys.get(name, 0)
                old_pct = 100.0 * base_count / base_rows
                new_pct = 100.0 * now / rows
                print(f"{old_pct:6.2f}% -> {new_pct:6.2f}%  {new_pct - old_pct:+6.2f}  {name}")

    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
