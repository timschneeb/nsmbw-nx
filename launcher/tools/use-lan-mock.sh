#!/usr/bin/env bash
# Point the console's HTTP mock at the LAN instead of the Tailscale route.
# Run from a terminal; sudo will ask for your password.
set -euo pipefail

SWITCH_IP="${SWITCH_IP:-192.168.178.177}"
SWITCH_ROOT="${SWITCH_ROOT:-/switch/nsmbw}"
IFACE="${IFACE:-wlan0}"
PORT="${PORT:-8000}"

if [[ $EUID -ne 0 ]]; then
    exec sudo -- "$0" "$@"
fi

host_ip=$(ip -4 -o addr show "$IFACE" | awk '{print $4}' | cut -d/ -f1 | head -1)
if [[ -z "$host_ip" ]]; then
    echo "no IPv4 address on $IFACE" >&2
    exit 1
fi

ip route replace "$SWITCH_IP/32" dev "$IFACE" table 52
route=$(ip route get "$SWITCH_IP")
if ! grep -q "dev $IFACE" <<<"$route"; then
    ip rule add to "$SWITCH_IP/32" lookup main priority 100 2>/dev/null || true
    route=$(ip route get "$SWITCH_IP")
fi
echo "$route"
if ! grep -q "dev $IFACE" <<<"$route"; then
    echo "the console still routes through another interface; not switching the catalogue" >&2
    exit 1
fi

if ! curl -sf "http://127.0.0.1:$PORT/assets.json" >/dev/null; then
    echo "the mock is not running on port $PORT" >&2
    echo "start it with: python3 launcher/tools/mock_release.py --port $PORT" >&2
    exit 1
fi

python3 - "$SWITCH_IP" "$host_ip" "$PORT" "$SWITCH_ROOT" <<'PY'
import io
import sys
from ftplib import FTP

switch_ip, host_ip, port, switch_root = sys.argv[1:5]
root = 'sdmc:' + switch_root.rstrip('/')
ftp = FTP()
ftp.connect(switch_ip, 5001, timeout=60)
ftp.login()

local = root + '/assets.local.json'
backup = root + '/assets.local.json.httpbak'
try:
    try:
        ftp.delete(backup)
    except Exception:
        pass
    ftp.rename(local, backup)
    print('moved assets.local.json to assets.local.json.httpbak')
except Exception as error:
    print('assets.local.json:', error)

url = f'http://{host_ip}:{port}/assets.json'
ftp.storbinary('STOR ' + root + '/assets.url', io.BytesIO((url + '\n').encode()))
print('assets.url =', url)
ftp.quit()
PY

echo
echo "Relaunch the launcher, or open Download games once, to load the LAN catalogue."
echo "Restore the SD catalogue with: ftp rename assets.local.json.httpbak assets.local.json"
