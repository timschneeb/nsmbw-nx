#!/usr/bin/env python3
"""Pull a PGO recording run's flat .gcda tree off the console and install it in the build tree.

The recording build sets GCOV_PREFIX=sdmc:/gcov and GCOV_PREFIX_STRIP=100, so libgcov (which
creates no directories) writes one file per translation unit into that single directory. Each
file is copied next to the .gcno of the same name, which is where -fprofile-use looks for it.
"""

import argparse
import ftplib
import os
import sys

REMOTE_DIR = "/sdmc:/gcov"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.178.177")
    parser.add_argument("--port", type=int, default=5001)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    notes = {}
    for root, _dirs, files in os.walk(args.build_dir):
        for name in files:
            if name.endswith(".gcno"):
                notes[name[: -len(".gcno")]] = root
    if not notes:
        sys.exit(f"no .gcno files under {args.build_dir}")

    ftp = ftplib.FTP()
    ftp.connect(args.host, args.port, timeout=60)
    ftp.login()
    listing = []
    ftp.retrlines(f"LIST {REMOTE_DIR}", listing.append)
    data = [line.split(None, 8)[-1] for line in listing if line and line[0] == "-"]
    data = [name for name in data if name.endswith(".gcda")]
    if not data:
        sys.exit(f"no .gcda files under {REMOTE_DIR}")

    installed = 0
    unmatched = 0
    for name in data:
        base = name[: -len(".gcda")]
        directory = notes.get(base)
        if directory is None:
            unmatched += 1
            continue
        target = os.path.join(directory, name)
        if args.dry_run:
            print(name, "->", target)
            continue
        with open(target, "wb") as out:
            ftp.retrbinary(f"RETR {REMOTE_DIR}/{name}", out.write)
        installed += 1
    ftp.quit()
    print(f"installed {installed} .gcda files, {unmatched} unmatched")
    if unmatched:
        print("unmatched files belong to objects no longer in the build tree", file=sys.stderr)


if __name__ == "__main__":
    main()
