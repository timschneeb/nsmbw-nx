#!/usr/bin/env python3
"""Pull the newest perf.csv from the console and diff it against a baseline.

The game appends one row per two second window to
switch:/nsmbw/WiiCompiled/Logs/<run>/perf.csv when [debug] stats is on.

Usage:
  perf_compare.py --save --label 1-1          record a baseline
  perf_compare.py --label 1-1                 compare against it
  perf_compare.py --csv perf.csv --label 1-1  compare a local file
"""

import argparse
import csv
import ftplib
import io
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "scripts" / "perf_baseline.json"
REMOTE = "switch:/nsmbw/WiiCompiled/Logs"


def newest_csv(host, port, timeout):
    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=timeout)
    ftp.login()
    ftp.cwd("/")
    ftp.cwd(REMOTE)
    runs = sorted(name.split("/")[-1] for name in ftp.nlst())
    for run in reversed(runs):
        try:
            ftp.cwd("/")
            ftp.cwd(f"{REMOTE}/{run}")
            buffer = io.BytesIO()
            ftp.retrbinary("RETR perf.csv", buffer.write)
            ftp.quit()
            return run, buffer.getvalue().decode("utf-8", "replace")
        except ftplib.error_perm:
            continue
    ftp.quit()
    raise SystemExit("no perf.csv on the console, run with [debug] stats = \"light\"")


def summarize(text):
    rows = list(csv.DictReader(io.StringIO(text)))
    if not rows:
        raise SystemExit("perf.csv is empty")
    fps = [float(row["fps"]) for row in rows]
    p50 = [float(row["p50"]) for row in rows]
    p95 = [float(row["p95"]) for row in rows]
    guest = [float(row["guest_ms"]) for row in rows if float(row["guest_ms"]) > 0]
    return {
        "build": rows[-1]["build"],
        "rows": len(rows),
        "fps": round(statistics.median(fps), 2),
        "p50": round(statistics.median(p50), 2),
        "p95": round(statistics.median(p95), 2),
        "worst": round(max(float(row["worst"]) for row in rows), 2),
        "guest_ms": round(statistics.median(guest), 2) if guest else 0.0,
    }


def load_baselines(path):
    if path.exists():
        return json.loads(path.read_text())
    return {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.178.177")
    parser.add_argument("--port", type=int, default=5001)
    parser.add_argument("--label", default="default", help="benchmark scene name")
    parser.add_argument("--csv", type=Path, help="local perf.csv instead of FTP")
    parser.add_argument("--save", action="store_true", help="store this run as the baseline")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--fps-drop", type=float, default=2.0, help="fail above this fps drop in percent")
    parser.add_argument("--p95-rise", type=float, default=5.0, help="fail above this p95 rise in percent")
    args = parser.parse_args()

    if args.csv:
        run, text = str(args.csv), args.csv.read_text()
    else:
        run, text = newest_csv(args.host, args.port, 40)

    result = summarize(text)
    baselines = load_baselines(args.baseline)

    if args.save:
        baselines[args.label] = result
        args.baseline.write_text(json.dumps(baselines, indent=2) + "\n")
        print(f"saved baseline {args.label}: {json.dumps(result)}")
        return 0

    base = baselines.get(args.label)
    if base is None:
        print(f"no baseline for {args.label}, run with --save first")
        print(f"run {run}: {json.dumps(result)}")
        return 2

    fps_drop = (base["fps"] - result["fps"]) / base["fps"] * 100.0 if base["fps"] else 0.0
    p95_rise = (result["p95"] - base["p95"]) / base["p95"] * 100.0 if base["p95"] else 0.0
    print(f"run {run}")
    print(f"{'metric':>10} {'baseline':>10} {'now':>10} {'delta':>10}")
    for key in ("fps", "p50", "p95", "worst", "guest_ms"):
        delta = result[key] - base[key]
        print(f"{key:>10} {base[key]:>10.2f} {result[key]:>10.2f} {delta:>+10.2f}")
    print(f"fps drop {fps_drop:+.1f}% (limit {args.fps_drop}%), p95 rise {p95_rise:+.1f}% (limit {args.p95_rise}%)")

    if fps_drop > args.fps_drop or p95_rise > args.p95_rise:
        print("REGRESSION")
        return 1
    print("no regression")
    return 0


if __name__ == "__main__":
    sys.exit(main())
