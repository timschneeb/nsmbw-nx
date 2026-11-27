"""Builds a Newer-style mod prelink output from a Riivolution pack.

    python newer_pack_prelink.py <riivolution pack folder> [<clean game folder>] [<output folder>]

The pack has no patched main.dol: Riivolution writes Loader.bin into low memory at 0x80001800
and patches main.dol in memory at boot. This script does the same offline:

- applies the pack's <memory> patches to the clean main.dol (the original= check selects the
  US entries, the other regions do not match),
- adds Loader.bin as a text section at 0x80001800 so the translator can translate it,
- leaves the phase slot pointing at 0x80001800,
- then runs newer_prelink.py for the REL, the mod blob and the function map.

--pack-folder names the folder inside the pack (NewerSMBW, XmasNewer, ...), --xml names the
Riivolution XML relative to the pack root. --dlcode-addr is the guest address the loader's
allocation lands at; --no-newer-fixes skips Newer 1.30's replay-heap and DVDDiskCheck patches.
"""
import argparse
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import newer_prelink as np

REPO = Path(__file__).resolve().parents[3]
LOADER_ADDR = 0x80001800


def pack_root(path: Path, folder: str, xml: str):
    if folder:
        pack = path / folder
        if not (pack / "NewerRes").is_dir():
            sys.exit(f"{pack} has no NewerRes folder")
        xml_path = path / xml if xml else (path / "riivolution" / "NewerSMBW.xml")
        return pack, xml_path

    for candidate, default_xml in ((path / "NewerSMBW", "NewerSMBW.xml"),
                                   (path / "XmasNewer", "XmasNewer.xml")):
        if (candidate / "NewerRes").is_dir():
            return candidate, path / "riivolution" / (xml or default_xml)
    if (path / "files" / "NewerRes").is_dir():
        return path, path / "riivolution" / (xml or "NewerSMBW.xml")
    for candidate in sorted(path.iterdir()):
        if candidate.is_dir() and (candidate / "NewerRes").is_dir():
            return candidate, path / "riivolution" / (xml or "NewerSMBW.xml")
    sys.exit(f"{path} does not look like a Riivolution pack")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pack", type=Path, help="extracted Riivolution pack folder")
    parser.add_argument("clean", nargs="?", type=Path, default=REPO.parent / "game",
                        help="clean SMNE01 folder (default: ../game)")
    parser.add_argument("output", nargs="?", type=Path, default=REPO.parent / "game-newer",
                        help="output folder (default: ../game-newer)")
    parser.add_argument("--pack-folder", default="",
                        help="folder inside the pack with NewerRes and Loader.bin")
    parser.add_argument("--xml", default="", help="Riivolution XML relative to the pack root")
    parser.add_argument("--loader", default="", help="Loader.bin relative to the pack root")
    parser.add_argument("--dlcode-addr", default="0x80DF8FC0",
                        help="guest address the loader allocates DLCode at (default: 0x80DF8FC0)")
    parser.add_argument("--variant", default="US_1", help="NewerRes region suffix (default: US_1)")
    parser.add_argument("--no-newer-fixes", action="store_true",
                        help="skip Newer 1.30's replay-heap and DVDDiskCheck patches")
    args = parser.parse_args()

    pack, xml_path = pack_root(args.pack, args.pack_folder, args.xml)
    loader = args.pack / args.loader if args.loader else pack / "Loader.bin"
    if not loader.is_file():
        sys.exit(f"{loader} is missing")
    if not xml_path.is_file():
        sys.exit(f"{xml_path} is missing")

    dol = np.Dol((args.clean / "main.dol").read_bytes())

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

    (args.output / "sys").mkdir(parents=True, exist_ok=True)
    (args.output / "files" / "NewerRes").mkdir(parents=True, exist_ok=True)
    (args.output / "sys" / "main.dol").write_bytes(dol.data)

    for name in (f"System{args.variant}.bin", f"DLCode{args.variant}.bin",
                 f"DLRelocs{args.variant}.bin"):
        src = pack / "NewerRes" / name
        if not src.is_file():
            sys.exit(f"{src} is missing")
        (args.output / "files" / "NewerRes" / name).write_bytes(src.read_bytes())

    script = Path(__file__).with_name("newer_prelink.py")
    command = [sys.executable, str(script), str(args.output), str(args.clean), str(args.output),
               "--dlcode-addr", args.dlcode_addr, "--variant", args.variant]
    if args.no_newer_fixes:
        command.append("--no-newer-fixes")
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
