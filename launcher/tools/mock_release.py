#!/usr/bin/env python3
"""Serve the release staging folders as a fake GitHub release.

The launcher reads <switch-root>/assets.url when it exists, so writing
http://<host>:<port>/assets.json there points it at this server. The generated
catalogue keeps every sha256 and size and only rewrites the download URLs.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import socket
import subprocess
import sys
from pathlib import Path


def detect_host(target: str) -> str:
    # Prefer a real LAN interface over Tailscale and container bridges.
    try:
        output = subprocess.check_output(
            ["ip", "-o", "-4", "addr", "show", "scope", "global"], text=True)
        addresses = []
        for line in output.splitlines():
            parts = line.split()
            if "inet" not in parts:
                continue
            name = parts[1].rstrip(":")
            address = parts[parts.index("inet") + 1].split("/")[0]
            addresses.append((name, address))
        for name, address in addresses:
            if name.startswith(("wl", "eth", "en")):
                return address
        for name, address in addresses:
            if not address.startswith("100."):
                return address
    except (OSError, subprocess.SubprocessError, ValueError):
        pass

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((target, 9))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def rewrite_urls(node, base_url: str, served: set[str]) -> None:
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "url" and isinstance(value, str) and value:
                name = value.rsplit("/", 1)[-1]
                node[key] = f"{base_url}/{name}"
                served.add(name)
            else:
                rewrite_urls(value, base_url, served)
    elif isinstance(node, list):
        for item in node:
            rewrite_urls(item, base_url, served)


def build_serve_dir(main_dir: Path, mods_dir: Path, catalogue: Path, serve_dir: Path,
                    base_url: str) -> set[str]:
    serve_dir.mkdir(parents=True, exist_ok=True)

    for source_dir in (main_dir, mods_dir):
        if not source_dir.is_dir():
            print(f"warning: {source_dir} does not exist", file=sys.stderr)
            continue
        for item in source_dir.iterdir():
            if not item.is_file():
                continue
            target = serve_dir / item.name
            if target.exists() or target.is_symlink():
                target.unlink()
            target.symlink_to(item.resolve())

    data = json.loads(catalogue.read_text())
    served: set[str] = set()
    rewrite_urls(data, base_url, served)

    for name in served:
        if not (serve_dir / name).exists():
            print(f"warning: catalogue references missing {name}", file=sys.stderr)

    assets_path = serve_dir / "assets.json"
    if assets_path.is_symlink() or assets_path.exists():
        assets_path.unlink()
    assets_path.write_text(json.dumps(data, indent=2) + "\n")
    return served


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[mock-release] %s %s\n" % (self.address_string(), fmt % args))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main", type=Path, default=Path("/tmp/opencode/release-main"))
    parser.add_argument("--mods", type=Path, default=Path("/tmp/opencode/release-mods"))
    parser.add_argument("--catalogue", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets.json")
    parser.add_argument("--serve-dir", type=Path, default=Path("/tmp/opencode/mock-release"))
    parser.add_argument("--port", type=int, default=8123)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--host", default=None,
                        help="address the Switch uses, defaults to the LAN address")
    parser.add_argument("--target", default="192.168.178.177",
                        help="address used to find the LAN interface")
    parser.add_argument("--base-url", default=None,
                        help="public base URL, for example a Cloudflare tunnel")
    parser.add_argument("--switch-root", default="/switch/nsmbw",
                        help="launcher folder on the SD card")
    args = parser.parse_args()

    host = args.host or detect_host(args.target)
    base_url = args.base_url or f"http://{host}:{args.port}"

    served = build_serve_dir(args.main, args.mods, args.catalogue, args.serve_dir, base_url)
    print(f"serving {len(served)} release file(s) from {args.serve_dir}", flush=True)
    print(f"assets.url content: {base_url}/assets.json", flush=True)
    print(flush=True)
    print("upload it with:", flush=True)
    print("  python3 - <<'PY'", flush=True)
    print("  from ftplib import FTP", flush=True)
    print("  import io", flush=True)
    print("  ftp = FTP(); ftp.connect(%r, 5001, timeout=60); ftp.login()" % args.target, flush=True)
    print("  ftp.storbinary('STOR sdmc:%s/assets.url'," % args.switch_root.rstrip('/'), flush=True)
    print(f"                   io.BytesIO(b'{base_url}/assets.json\\n'))", flush=True)
    print("  ftp.quit()")
    print("  PY")
    print(flush=True)
    print("remove assets.local.json and assets.cache.json before testing.", flush=True)
    print(flush=True)

    handler = functools.partial(Handler, directory=str(args.serve_dir))
    server = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    server.daemon_threads = True
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
