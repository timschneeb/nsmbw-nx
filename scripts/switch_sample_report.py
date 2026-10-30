#!/usr/bin/env python3
"""Summarize the Switch shadow sampler trace."""

import argparse
import collections
from pathlib import Path


def load_map(path):
    names = {}
    with Path(path).open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split(None, 1)
            if len(parts) != 2 or len(parts[0]) != 8:
                continue
            try:
                address = int(parts[0], 16)
            except ValueError:
                continue
            name = parts[1].strip()
            if name:
                names[address] = name
    return names


def display_name(address, registry_name, names):
    if address in names:
        return names[address]
    if registry_name.startswith("func_") and len(registry_name) == 13:
        try:
            mapped = names.get(int(registry_name[5:], 16))
        except ValueError:
            mapped = None
        if mapped:
            return mapped
    return registry_name or "?"


def load(path, scene):
    by_function = collections.Counter()
    by_scene = collections.Counter()
    rows = 0
    with Path(path).open(encoding="utf-8", errors="replace") as handle:
        next(handle, None)
        for line in handle:
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 6:
                continue
            sample_scene = fields[0]
            if scene and sample_scene != scene:
                continue
            rows += 1
            address_text, function = fields[3], fields[5]
            try:
                address = int(address_text, 16)
            except ValueError:
                address = 0
            if address == 0:
                continue
            by_function[(address, function)] += 1
            by_scene[sample_scene] += 1
    return rows, by_function, by_scene


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("samples")
    parser.add_argument("--map", default="projects/nsmbw/MAP.txt")
    parser.add_argument("--scene", default="", help="scene tag, e.g. S005; empty for all")
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--compare", help="baseline shadow-samples.tsv")
    args = parser.parse_args()

    names = load_map(args.map) if Path(args.map).is_file() else {}
    scene = args.scene or None
    rows, by_function, by_scene = load(args.samples, scene)
    if rows == 0:
        raise SystemExit(f"no samples for scene {scene or 'all'} in {args.samples}")

    print(f"samples {args.samples}")
    print(f"rows    {rows} ({scene or 'all scenes'})")
    if by_scene:
        print("scenes  " + ", ".join(f"{key}:{value}" for key, value in sorted(by_scene.items())))

    ranked = by_function.most_common(args.top)
    for (address, registry_name), count in ranked:
        name = display_name(address, registry_name, names)
        print(f"{100.0 * count / rows:6.2f}%  {count:8d}  {address:08X}  {name}")

    if args.compare:
        base_rows, base_functions, _ = load(args.compare, scene)
        if base_rows:
            print(f"compare {args.compare} ({base_rows} samples)")
            for (address, registry_name), count in ranked:
                base_count = base_functions.get((address, registry_name), 0)
                old = 100.0 * base_count / base_rows
                new = 100.0 * count / rows
                name = display_name(address, registry_name, names)
                print(f"{old:6.2f}% -> {new:6.2f}%  {new - old:+6.2f}  {address:08X}  {name}")


if __name__ == "__main__":
    main()
