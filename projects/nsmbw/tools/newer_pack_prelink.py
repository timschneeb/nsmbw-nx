"""Builds the Newer prelink output from the official Riivolution pack.

    python newer_pack_prelink.py <riivolution pack folder> [<clean game folder>] [<output folder>]

The pack has no Newer main.dol: Riivolution writes Loader.bin into low memory at 0x80001800
and patches main.dol in memory at boot. This script does the same offline:

- applies the pack's <memory> patches to the clean main.dol (the original= check selects the
  US entries, the other regions do not match),
- adds Loader.bin as a text section at 0x80001800 so the translator can translate it,
- leaves the phase slot pointing at 0x80001800,
- then runs newer_prelink.py for the REL, the Newer blob and the function map.
"""
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import newer_prelink as np

REPO = Path(__file__).resolve().parents[3]
CLEAN = Path(sys.argv[2]) if len(sys.argv) > 2 else REPO.parent / "game"
OUT = Path(sys.argv[3]) if len(sys.argv) > 3 else REPO.parent / "game-newer"
LOADER_ADDR = 0x80001800


def pack_root(path: Path):
    if (path / "NewerSMBW" / "NewerRes").is_dir():
        return path / "NewerSMBW", path / "riivolution" / "NewerSMBW.xml"
    if (path / "files" / "NewerRes").is_dir():
        return path, path / "riivolution" / "NewerSMBW.xml"
    sys.exit(f"{path} does not look like the Newer Riivolution pack")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    pack, xml_path = pack_root(Path(sys.argv[1]))
    loader = pack / "Loader.bin"
    if not loader.is_file():
        sys.exit(f"{loader} is missing")

    dol = np.Dol((CLEAN / "main.dol").read_bytes())

    applied = 0
    skipped = 0
    for node in ET.parse(xml_path).getroot().iter("memory"):
        if node.get("valuefile"):
            continue

        offset = node.get("offset")
        value = node.get("value")
        if not offset or not value:
            continue

        addr = int(offset, 16)
        data = bytes.fromhex(value)
        hit = dol.locate(addr)
        if not hit:
            skipped += 1
            continue

        _, off = hit
        original = node.get("original")
        if original and dol.data[off:off + len(data)] != bytes.fromhex(original):
            skipped += 1
            continue

        dol.data[off:off + len(data)] = data
        applied += 1

    text, off = dol.locate(np.PHASE_SLOT)
    slot = struct.unpack_from(">I", dol.data, off)[0]
    if slot != LOADER_ADDR:
        sys.exit(f"phase slot is 0x{slot:08X}, the pack's loader patch did not apply")
    if dol.locate(LOADER_ADDR):
        sys.exit(f"0x{LOADER_ADDR:08X} is already mapped in the clean main.dol")

    blob = loader.read_bytes()
    if LOADER_ADDR + len(blob) > 0x80004000:
        sys.exit("Loader.bin is larger than the low memory it targets")

    # add the loader as the third text section so it is translated like game code.
    while len(dol.data) % 0x20:
        dol.data.append(0)
    text_offset = len(dol.data)
    dol.data += blob

    struct.pack_into(">I", dol.data, 0x08, text_offset)
    struct.pack_into(">I", dol.data, 0x50, LOADER_ADDR)
    struct.pack_into(">I", dol.data, 0x98, len(blob))

    print(f"applied {applied} pack memory patches ({skipped} other region entries skipped)")
    print(f"loader {len(blob)} bytes at 0x{LOADER_ADDR:08X}")

    (OUT / "sys").mkdir(parents=True, exist_ok=True)
    (OUT / "files" / "NewerRes").mkdir(parents=True, exist_ok=True)
    (OUT / "sys" / "main.dol").write_bytes(dol.data)

    for name in ("SystemUS_1.bin", "DLCodeUS_1.bin", "DLRelocsUS_1.bin"):
        src = pack / "NewerRes" / name
        if not src.is_file():
            sys.exit(f"{src} is missing")
        (OUT / "files" / "NewerRes" / name).write_bytes(src.read_bytes())

    script = Path(__file__).with_name("newer_prelink.py")
    subprocess.run([sys.executable, str(script), str(OUT), str(CLEAN), str(OUT)], check=True)


if __name__ == "__main__":
    main()
