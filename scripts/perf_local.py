#!/usr/bin/env python3
"""Run one local NSMBW perf scenario and summarize the [vi] perf rows.

The PC is GPU-bound at 4x, so guest CPU is the primary signal. Use the same
data home as the manual runs so the 4x Vulkan config is reused.
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENARIOS = {
    "title": ("5000:TWO:300,8500:TWO:300", 20, "0x000"),
    "stage": ("5000:TWO:300,8500:TWO:300,11500:TWO:300", 37, "0x005"),
    # Strap warning dismissed, then idle: the attract mode loads a random level by itself.
    "demo": ("5000:TWO:300,8500:TWO:300", 300, "0x005"),
}

PERF_RE = re.compile(
    r"\[vi\] perf: p50 (?P<p50>[\d.]+) p95 (?P<p95>[\d.]+) p99 (?P<p99>[\d.]+) "
    r"worst (?P<worst>[\d.]+) ms; guest cpu (?P<guest>[\d.]+) ms.*?"
    r"gx (?P<gx>[\d.]+) ms, gx fifo (?P<fifo>[\d.]+) ms.*?"
    r"draw [\d.]+ x (?P<draws>\d+), (?:mmio [\d.]+ k, )?(?P<over20>\d+) over 20 ms"
)
SCENE_RE = re.compile(r"\[vi\] scene -> 0x([0-9A-Fa-f]{3})")


def parse_rows(text):
    scene = None
    rows = []
    for line in text.splitlines():
        match = SCENE_RE.search(line)
        if match:
            scene = "0x" + match.group(1).upper()
            continue
        match = PERF_RE.search(line)
        if not match:
            continue
        row = {name: float(value) for name, value in match.groupdict().items()}
        row["draws"] = int(row["draws"])
        row["over20"] = int(row["over20"])
        row["scene"] = scene
        rows.append(row)
    return rows


def medians(rows):
    if not rows:
        raise SystemExit("no matching [vi] perf rows")
    result = {
        "rows": len(rows),
        "p50_ms": statistics.median(row["p50"] for row in rows),
        "p95_ms": statistics.median(row["p95"] for row in rows),
        "p99_ms": statistics.median(row["p99"] for row in rows),
        "worst_ms": statistics.median(row["worst"] for row in rows),
        "guest_ms": statistics.median(row["guest"] for row in rows),
        "gx_ms": statistics.median(row["gx"] for row in rows),
        "fifo_ms": statistics.median(row["fifo"] for row in rows),
        "draws": statistics.median(row["draws"] for row in rows),
        "over20": statistics.median(row["over20"] for row in rows),
    }
    result["fps"] = 1000.0 / result["p50_ms"] if result["p50_ms"] else 0.0
    return result


def compare(label, current, baseline):
    print(f"compare {label} vs {baseline.get('label', 'baseline')}:")
    for key in ("guest_ms", "p50_ms", "p95_ms", "p99_ms", "worst_ms", "gx_ms", "fifo_ms", "fps"):
        old = baseline.get(key)
        new = current.get(key)
        if not old or not new:
            continue
        delta = (new - old) / old * 100.0
        print(f"  {key:8s} {old:8.3f} -> {new:8.3f}  {delta:+6.1f}%")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", choices=sorted(SCENARIOS))
    parser.add_argument("--binary", default=str(ROOT / "build-linux" / "WiiCompiled"))
    parser.add_argument("--data-home", default="/tmp/opencode/nsmbw-perf/data")
    parser.add_argument("--log-dir", default="/tmp/opencode/nsmbw-perf/logs")
    parser.add_argument("--seconds", type=int)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--last", type=int, default=6)
    parser.add_argument("--label", default="run")
    parser.add_argument("--profile", help="value for NSMBW_PROFILE, e.g. 15:11")
    parser.add_argument("--profile-threads", help="value for NSMBW_PROFILE_THREADS, e.g. game,ax-mix")
    parser.add_argument("--save")
    parser.add_argument("--compare")
    args = parser.parse_args()

    script, default_seconds, expected_scene = SCENARIOS[args.scenario]
    seconds = args.seconds or default_seconds
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    config = Path(args.data_home) / "WiiCompiled" / "Config.toml"
    if not config.is_file():
        raise SystemExit(f"missing config: {config}")

    env = os.environ.copy()
    env["XDG_DATA_HOME"] = args.data_home
    env["NSMBW_INPUT_SCRIPT"] = script
    env.pop("NSMBW_PROFILE_PC", None)
    env.pop("NSMBW_TEX_STATS", None)
    env.pop("NSMBW_VTX_STATS", None)
    if args.profile:
        env["NSMBW_PROFILE"] = args.profile
    else:
        env.pop("NSMBW_PROFILE", None)
    if args.profile_threads:
        env["NSMBW_PROFILE_THREADS"] = args.profile_threads
    else:
        env.pop("NSMBW_PROFILE_THREADS", None)

    summaries = []
    for run in range(args.runs):
        stamp = time.strftime("%H%M%S")
        log_path = log_dir / f"{args.label}-{args.scenario}-{stamp}-{run}.log"
        with log_path.open("w") as log:
            try:
                subprocess.run(
                    ["timeout", str(seconds), args.binary],
                    cwd=ROOT,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=seconds + 5,
                )
            except subprocess.TimeoutExpired:
                pass
        text = log_path.read_text(errors="replace")
        rows = [row for row in parse_rows(text) if row["scene"] == expected_scene]
        rows = rows[-args.last :]
        summary = medians(rows)
        summary.update(
            {
                "label": args.label,
                "scenario": args.scenario,
                "scene": expected_scene,
                "run": run,
                "log": str(log_path),
            }
        )
        summaries.append(summary)
        print(
            f"{args.label} run {run}: scene {expected_scene}, rows {summary['rows']}, "
            f"p50 {summary['p50_ms']:.2f}, p95 {summary['p95_ms']:.2f}, p99 {summary['p99_ms']:.2f}, "
            f"worst {summary['worst_ms']:.2f}, guest {summary['guest_ms']:.2f}, "
            f"gx {summary['gx_ms']:.2f}, fifo {summary['fifo_ms']:.2f}, "
            f"fps {summary['fps']:.1f}"
        )

    combined = {
        key: statistics.median(summary[key] for summary in summaries)
        for key in ("p50_ms", "p95_ms", "p99_ms", "worst_ms", "guest_ms", "gx_ms", "fifo_ms", "draws", "over20")
    }
    combined["fps"] = 1000.0 / combined["p50_ms"] if combined["p50_ms"] else 0.0
    combined["label"] = args.label
    combined["scenario"] = args.scenario
    combined["scene"] = expected_scene
    combined["runs"] = args.runs
    print(json.dumps(combined, indent=2, sort_keys=True))

    if args.save:
        Path(args.save).write_text(json.dumps(combined, indent=2, sort_keys=True) + "\n")
    if args.compare:
        compare(args.label, combined, json.loads(Path(args.compare).read_text()))


if __name__ == "__main__":
    main()
