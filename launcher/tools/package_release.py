#!/usr/bin/env python3
"""Stage the Switch release assets and regenerate the catalogue.

Build the launcher and both game NROs first, then run:

    python3 launcher/tools/package_release.py

The default paths match the in-tree build directories. The script writes
``launcher/assets.json``, an SD-card ZIP and the raw release files under
``dist/release`` so hashes and versions cannot go stale between a build and an
upload.

Main release: ``NSMBW-switch.zip`` and ``assets.json`` only.
Modules release: every standalone binary, resource and debug symbol.

It does not upload anything. It writes ``upload-main.sh`` and
``upload-modules.sh`` with the matching ``gh release create`` commands.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shlex
import shutil
import struct
import sys
import zipfile
from pathlib import Path

from make_assets import build_assets, write_assets

REPO_ROOT = Path(__file__).resolve().parents[2]
VERSION_FILE = REPO_ROOT / "VERSION"
DEFAULT_LAUNCHER = REPO_ROOT / "build-launcher" / "launcher.nro"
DEFAULT_VANILLA = REPO_ROOT / "build-switch-product2" / "NSMBW.nro"
DEFAULT_NEWER = REPO_ROOT / "build-switch-newer" / "NSMBW.nro"
DEFAULT_ROMFS = Path("/tmp/opencode/newersmbw.romfs.zst")
DEFAULT_IMAGE = Path("/tmp/opencode/newersmbw.romfs")
DEFAULT_BANNER = REPO_ROOT / "launcher" / "assets" / "banners" / "newersmbw.png"
DEFAULT_VANILLA_SYMBOLS = REPO_ROOT / "build-switch-product2" / "dist" / "debug-symbols.zip"
DEFAULT_NEWER_SYMBOLS = REPO_ROOT / "build-switch-newer" / "dist" / "debug-symbols.zip"
DEFAULT_SOURCE_ASSETS = REPO_ROOT / "launcher" / "assets.json"
MODS_DIR = REPO_ROOT / "projects" / "nsmbw" / "mods"

# NacpStruct.display_version in libnx.
NACP_DISPLAY_VERSION = 0x3060
INSTALL_TEXT = """New Super Mario Bros. Wii for Nintendo Switch
=============================================

1. Copy the "switch" folder in this ZIP to the root of your SD card. The
   launcher is at switch/nsmbw/launcher.nro.

2. Start the NSMBW forwarder from the home menu, or open
   switch/nsmbw/launcher.nro from hbmenu.

3. On first start, place a USA SMNE01 revision 1 disc image (.iso, .wbfs,
   .ciso, .wia or .rvz) next to launcher.nro. The launcher extracts the shared
   base game data to switch/nsmbw/discs/SMNE01.

4. Mods such as Newer Super Mario Bros. Wii and Newer Holiday Special are
   installed from "Download games" in the launcher. The base game data is
   shared; each mod only adds its own NRO and resource image.
"""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_version_file() -> str:
    version = VERSION_FILE.read_text(encoding="utf-8").strip()
    if not version:
        raise ValueError("%s is empty" % VERSION_FILE)
    return version


def nro_version(path: Path) -> str:
    data = path.read_bytes()
    if data[:4] == b"NRO0":
        base = 0
    elif data[16:20] == b"NRO0":
        base = 16
    else:
        raise ValueError("%s is not an NRO" % path)

    if len(data) < base + 16:
        raise ValueError("%s is too short for an NRO header" % path)
    header_size = struct.unpack_from("<I", data, base + 8)[0]
    if header_size + 0x28 > len(data) or data[header_size:header_size + 4] != b"ASET":
        raise ValueError("%s has no asset section" % path)

    nacp_offset = struct.unpack_from("<Q", data, header_size + 24)[0]
    start = header_size + nacp_offset + NACP_DISPLAY_VERSION
    if start + 0x10 > len(data):
        raise ValueError("%s has no NACP display version" % path)
    raw = data[start:start + 0x10].split(b"\0", 1)[0]
    try:
        version = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("%s has a non-ASCII NACP version" % path) from exc
    if not version:
        raise ValueError("%s has an empty NACP version" % path)
    return version


def release_url(repo: str, tag: str, pinned: bool, name: str) -> str:
    if pinned:
        return "https://github.com/%s/releases/download/%s/%s" % (repo, tag, name)
    return "https://github.com/%s/releases/latest/download/%s" % (repo, name)


def repo_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else REPO_ROOT / path


def load_mods() -> list[dict]:
    mods = []
    if not MODS_DIR.is_dir():
        return mods
    for path in sorted(MODS_DIR.glob("*.json")):
        mod = json.loads(path.read_text(encoding="utf-8"))
        assets = mod.get("assets", {})
        mod["nro_path"] = repo_path(mod["nro"])
        mod["symbols_path"] = repo_path(mod["symbols"])
        mod["romfs_path"] = repo_path(mod["romfs_zst"])
        mod["image_path"] = repo_path(mod["image"])
        mod["banner_path"] = repo_path(mod["banner_file"])
        for key in ("nro", "romfs", "banner", "symbols"):
            if not assets.get(key):
                raise ValueError("%s has no assets.%s" % (path, key))
        mods.append(mod)
    return mods


def mod_source_spec(args, version: str, mods: list[dict]) -> dict:
    games = {}
    for mod in mods:
        assets = mod["assets"]
        games[mod["id"]] = {
            "name": mod["name"],
            "author": mod["author"],
            "nro": {
                "recomp_version": version,
                "game_version": mod["game_version"],
                "region": args.region,
                "file": str(mod["nro_path"]),
                "url": release_url(args.modules_repo, args.tag, True, assets["nro"]),
            },
            "romfs": {
                "game_version": mod["game_version"],
                "region": args.region,
                "file": str(mod["romfs_path"]),
                "image": str(mod["image_path"]),
                "url": release_url(args.modules_repo, args.tag, True, assets["romfs"]),
            },
            "banner": {
                "file": str(mod["banner_path"]),
                "url": release_url(args.modules_repo, args.tag, True, assets["banner"]),
            },
        }
    return {"recomp_version": version, "games": games}


def load_base_assets(path: Path, modules_repo: str, base_tag: str, version: str) -> dict:
    assets = json.loads(path.read_text(encoding="utf-8"))

    def pin(component: dict) -> None:
        url = component.get("url", "")
        if url:
            component["url"] = release_url(modules_repo, base_tag, True, url.rsplit("/", 1)[-1])

    pin(assets["launcher"])
    for game in assets.get("games", {}).values():
        for component in game.get("components", {}).values():
            pin(component)
        if game.get("banner"):
            pin(game["banner"])
    assets["recomp_version"] = version
    return assets


def source_spec(args, version: str, launcher_version: str, mods: list[dict]) -> dict:
    games = {
        "vanilla": {
            "name": "New Super Mario Bros. Wii",
            "author": "Nintendo",
            "nro": {
                "recomp_version": version,
                "game_version": args.vanilla_game_version,
                "region": args.region,
                "file": str(args.vanilla),
                "url": release_url(args.modules_repo, args.tag, args.pinned, "vanilla-game.nro"),
            },
        },
        "newersmbw": {
            "name": "Newer Super Mario Bros. Wii",
            "author": "Newer Team",
            "nro": {
                "recomp_version": version,
                "game_version": args.newer_game_version,
                "region": args.region,
                "file": str(args.newer),
                "url": release_url(args.modules_repo, args.tag, args.pinned,
                                   "newersmbw-game.nro"),
            },
            "romfs": {
                "game_version": args.newer_romfs_version or args.newer_game_version,
                "region": args.region,
                "file": str(args.romfs),
                "image": str(args.image),
                "url": release_url(args.modules_repo, args.tag, args.pinned,
                                   "newersmbw.romfs.zst"),
            },
            "banner": {
                "file": str(args.banner),
                "url": release_url(args.modules_repo, args.tag, args.pinned,
                                   "newersmbw-banner.png"),
            },
        },
    }
    games.update(mod_source_spec(args, version, mods)["games"])
    return {
        "recomp_version": version,
        "launcher": {
            "version": launcher_version,
            "file": str(args.launcher),
            "url": release_url(args.modules_repo, args.tag, args.pinned, "launcher.nro"),
        },
        "games": games,
    }


def copy_checked(source: Path, target: Path) -> None:
    if not source.is_file():
        raise ValueError("missing %s" % source)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def stage_sd_zip(main_dir: Path, work_dir: Path, launcher: Path, assets_path: Path,
                 banners: list[tuple[str, Path]], vanilla: Path) -> Path:
    stage = work_dir / "switch" / "nsmbw"
    (stage / "banners").mkdir(parents=True, exist_ok=True)
    (stage / "games" / "vanilla").mkdir(parents=True, exist_ok=True)
    copy_checked(launcher, stage / "launcher.nro")
    copy_checked(assets_path, stage / "assets.json")
    for game_id, banner in banners:
        copy_checked(banner, stage / "banners" / (game_id + ".png"))
    copy_checked(vanilla, stage / "games" / "vanilla" / "game.nro")
    (stage / "INSTALL.txt").write_text(INSTALL_TEXT, encoding="utf-8")

    zip_path = main_dir / "NSMBW-switch.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(work_dir).as_posix())
    shutil.rmtree(work_dir)
    return zip_path


def file_record(path: Path) -> dict:
    return {"name": path.name, "sha256": sha256(path), "size": path.stat().st_size}


def gh_script(path: Path, repo: str, tag: str, files: list[Path], notes: str,
              create_repo: bool = False) -> None:
    names = " ".join(shlex.quote(str(f)) for f in files)
    lines = [
        "#!/usr/bin/env bash",
        "set -euo pipefail",
        "# Generated by launcher/tools/package_release.py",
    ]
    if create_repo:
        lines.append("# Create the repository once if it does not exist:")
        lines.append("# gh repo create %s --public" % repo)
    lines.append("gh release create %s --repo %s --title %s --notes %s %s" % (
        shlex.quote(tag), repo, shlex.quote(tag), shlex.quote(notes), names))
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    path.chmod(0o755)


def stage_mod_release(args, version: str) -> int:
    if args.base_assets is None or args.base_vanilla is None:
        raise ValueError("--mod-only needs --base-assets and --base-vanilla")
    mods = load_mods()
    base = load_base_assets(args.base_assets, args.modules_repo, args.base_tag, version)
    mod_spec = mod_source_spec(args, version, mods)
    mod_spec["launcher"] = {
        "version": base["launcher"].get("version", "1.0.0"),
        "file": str(args.launcher),
        "url": base["launcher"].get("url", ""),
    }
    mod_games = build_assets(mod_spec)["games"]
    base["games"].update(mod_games)
    launcher_version = args.launcher_version or base["launcher"].get("version")
    if not launcher_version:
        raise ValueError("no launcher version in the base catalogue")

    def expect_nro(path: Path, expected: str) -> None:
        actual = nro_version(path)
        if actual != expected:
            raise ValueError("%s NACP is %s, expected %s" % (path, actual, expected))

    expect_nro(args.launcher, launcher_version)
    expect_nro(args.base_vanilla, args.base_vanilla_version)
    for mod in mods:
        expect_nro(mod["nro_path"], version)

    if sha256(args.launcher) != base["launcher"]["sha256"]:
        raise ValueError("the launcher is not the one in the base catalogue")
    if sha256(args.base_vanilla) != base["games"]["vanilla"]["components"]["nro"]["sha256"]:
        raise ValueError("the base vanilla NRO is not the one in the base catalogue")
    if sha256(args.banner) != base["games"]["newersmbw"]["banner"]["sha256"]:
        raise ValueError("the Newer banner is not the one in the base catalogue")

    required = [args.launcher, args.base_vanilla, args.banner]
    for mod in mods:
        required += [mod["nro_path"], mod["romfs_path"], mod["image_path"], mod["banner_path"]]
        if not args.skip_symbols:
            required.append(mod["symbols_path"])
    for path in required:
        if not path.is_file():
            raise ValueError("missing %s" % path)

    if args.out.exists():
        shutil.rmtree(args.out)
    main_dir = args.out / "main"
    modules_dir = args.out / "modules"
    main_dir.mkdir(parents=True)
    modules_dir.mkdir(parents=True)

    write_assets(base, main_dir / "assets.json")
    if not args.no_update_source:
        write_assets(base, args.source_assets)

    for mod in mods:
        mod_assets = mod["assets"]
        copy_checked(mod["nro_path"], modules_dir / mod_assets["nro"])
        copy_checked(mod["romfs_path"], modules_dir / mod_assets["romfs"])
        copy_checked(mod["banner_path"], modules_dir / mod_assets["banner"])
        if not args.skip_symbols:
            copy_checked(mod["symbols_path"], modules_dir / mod_assets["symbols"])

    banners = [("newersmbw", args.banner)]
    banners += [(mod["id"], mod["banner_path"]) for mod in mods]
    zip_path = stage_sd_zip(main_dir, args.out / ".stage", args.launcher,
                            main_dir / "assets.json", banners, args.base_vanilla)

    main_files = [zip_path, main_dir / "assets.json"]
    modules_files = []
    for mod in mods:
        mod_assets = mod["assets"]
        modules_files += [modules_dir / mod_assets["nro"], modules_dir / mod_assets["romfs"],
                          modules_dir / mod_assets["banner"]]
        if not args.skip_symbols:
            modules_files.append(modules_dir / mod_assets["symbols"])

    manifest = {
        "tag": args.tag,
        "version": version,
        "main_repo": args.main_repo,
        "modules_repo": args.modules_repo,
        "mod_only": True,
        "base_tag": args.base_tag,
        "main": [file_record(path) for path in main_files],
        "modules": [file_record(path) for path in modules_files],
        "catalogue": base,
    }
    (args.out / "release.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                           encoding="utf-8")

    gh_script(args.out / "upload-main.sh", args.main_repo, args.tag, main_files,
              "NSMBW Switch 1.2.0: Newer Holiday Special. Launcher, NSMBW and Newer remain 1.1.0.")
    gh_script(args.out / "upload-modules.sh", args.modules_repo, args.tag, modules_files,
              "NSMBW Switch 1.2.0 Holiday Special runtime, resources and debug symbols.",
              create_repo=True)

    print("version    %s" % version)
    print("catalogue  %s" % (main_dir / "assets.json"))
    print("zip        %s" % zip_path)
    for path in main_files + modules_files:
        print("asset      %s  %d bytes" % (path, path.stat().st_size))
    print("manifest   %s" % (args.out / "release.json"))
    print("upload     %s" % (args.out / "upload-main.sh"))
    print("upload     %s" % (args.out / "upload-modules.sh"))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "dist" / "release")
    parser.add_argument("--launcher", type=Path, default=DEFAULT_LAUNCHER)
    parser.add_argument("--vanilla", type=Path, default=DEFAULT_VANILLA)
    parser.add_argument("--newer", type=Path, default=DEFAULT_NEWER)
    parser.add_argument("--romfs", type=Path, default=DEFAULT_ROMFS,
                        help="compressed Newer RomFS (newersmbw.romfs.zst)")
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE,
                        help="uncompressed Newer RomFS used for image_sha256/image_size")
    parser.add_argument("--banner", type=Path, default=DEFAULT_BANNER)
    parser.add_argument("--vanilla-symbols", type=Path, default=DEFAULT_VANILLA_SYMBOLS)
    parser.add_argument("--newer-symbols", type=Path, default=DEFAULT_NEWER_SYMBOLS)
    parser.add_argument("--skip-symbols", action="store_true",
                        help="do not require or copy debug symbol archives")
    parser.add_argument("--launcher-only", action="store_true",
                        help="publish only the launcher and catalogue, keep game assets on --game-tag")
    parser.add_argument("--game-tag", default="v0.0.1",
                        help="modules tag that keeps the game assets for --launcher-only")
    parser.add_argument("--mod-only", action="store_true",
                        help="publish only mod assets and preserve the base catalogue")
    parser.add_argument("--base-assets", type=Path,
                        help="base catalogue to preserve, for example the v1.1.0 assets.json")
    parser.add_argument("--base-tag", default="v1.1.0",
                        help="modules tag that keeps the unchanged base assets")
    parser.add_argument("--base-vanilla", type=Path,
                        help="unchanged vanilla NRO copied into the main ZIP")
    parser.add_argument("--base-vanilla-version", default="1.1.0",
                        help="NACP version of the unchanged vanilla NRO")
    parser.add_argument("--source-assets", type=Path, default=DEFAULT_SOURCE_ASSETS,
                        help="launcher/assets.json to update")
    parser.add_argument("--no-update-source", action="store_true",
                        help="do not overwrite launcher/assets.json")
    parser.add_argument("--main-repo", default="timschneeb/nsmbw-nx")
    parser.add_argument("--modules-repo", default="timschneeb/nsmbw-nx-modules")
    parser.add_argument("--tag", default="v0.0.1")
    parser.add_argument("--pinned", action="store_true",
                        help="use /releases/download/<tag>/ instead of /releases/latest/download/")
    parser.add_argument("--vanilla-game-version", default="1.0.0")
    parser.add_argument("--newer-game-version", default="1.30")
    parser.add_argument("--newer-romfs-version", default="",
                        help="defaults to --newer-game-version")
    parser.add_argument("--region", default="U",
                        help="region the NROs were recompiled from (U, E, J, ...)")
    parser.add_argument("--launcher-version", default="",
                        help="launcher NACP version when it differs from VERSION (a game-only update)")
    args = parser.parse_args()

    version = read_version_file()
    if args.tag.lstrip("v") != version:
        raise ValueError("tag %s does not match VERSION %s" % (args.tag, version))
    if args.mod_only:
        return stage_mod_release(args, version)
    launcher_version = args.launcher_version or version
    mods = [] if args.launcher_only else load_mods()

    required = [args.launcher, args.vanilla, args.banner]
    if not args.launcher_only:
        required += [args.newer, args.romfs, args.image]
        for mod in mods:
            required += [mod["nro_path"], mod["romfs_path"], mod["image_path"],
                         mod["banner_path"]]
    for path in required:
        if not path.is_file():
            raise ValueError("missing %s" % path)
    if not args.skip_symbols and not args.launcher_only:
        symbols = [args.vanilla_symbols, args.newer_symbols]
        symbols += [mod["symbols_path"] for mod in mods]
        for path in symbols:
            if not path.is_file():
                raise ValueError("missing %s (use --skip-symbols to omit)" % path)

    versioned = [(args.launcher, launcher_version)]
    if not args.launcher_only:
        versioned += [(args.vanilla, version), (args.newer, version)]
        versioned += [(mod["nro_path"], version) for mod in mods]
    for path, expected in versioned:
        actual = nro_version(path)
        if actual != expected:
            raise ValueError("%s NACP is %s, expected %s" % (path, actual, expected))

    if args.launcher_only:
        assets = json.loads(args.source_assets.read_text(encoding="utf-8"))
        assets["recomp_version"] = version
        assets["launcher"] = {
            "version": launcher_version,
            "url": release_url(args.modules_repo, args.tag, args.pinned, "launcher.nro"),
            "sha256": sha256(args.launcher),
            "size": args.launcher.stat().st_size,
        }
        for game in assets.get("games", {}).values():
            components = list(game.get("components", {}).values())
            if game.get("banner"):
                components.append(game["banner"])
            for component in components:
                url = component.get("url", "")
                if not url:
                    continue
                name = url.rsplit("/", 1)[-1]
                component["url"] = release_url(args.modules_repo, args.game_tag, True, name)
    else:
        assets = build_assets(source_spec(args, version, launcher_version, mods))

    if args.out.exists():
        shutil.rmtree(args.out)
    main_dir = args.out / "main"
    modules_dir = args.out / "modules"
    main_dir.mkdir(parents=True)
    modules_dir.mkdir(parents=True)

    write_assets(assets, main_dir / "assets.json")
    if not args.no_update_source:
        write_assets(assets, args.source_assets)

    copy_checked(args.launcher, modules_dir / "launcher.nro")
    if not args.launcher_only:
        copy_checked(args.vanilla, modules_dir / "vanilla-game.nro")
        copy_checked(args.newer, modules_dir / "newersmbw-game.nro")
        copy_checked(args.romfs, modules_dir / "newersmbw.romfs.zst")
        copy_checked(args.banner, modules_dir / "newersmbw-banner.png")
        if not args.skip_symbols:
            copy_checked(args.vanilla_symbols, modules_dir / "vanilla-debug-symbols.zip")
            copy_checked(args.newer_symbols, modules_dir / "newersmbw-debug-symbols.zip")
        for mod in mods:
            mod_assets = mod["assets"]
            copy_checked(mod["nro_path"], modules_dir / mod_assets["nro"])
            copy_checked(mod["romfs_path"], modules_dir / mod_assets["romfs"])
            copy_checked(mod["banner_path"], modules_dir / mod_assets["banner"])
            if not args.skip_symbols:
                copy_checked(mod["symbols_path"], modules_dir / mod_assets["symbols"])

    banners = [("newersmbw", args.banner)]
    banners += [(mod["id"], mod["banner_path"]) for mod in mods]
    zip_path = stage_sd_zip(main_dir, args.out / ".stage", args.launcher,
                            main_dir / "assets.json", banners, args.vanilla)

    # The main release is the user-facing ZIP and the catalogue only.
    main_files = [zip_path, main_dir / "assets.json"]
    modules_files = [modules_dir / "launcher.nro"]
    if not args.launcher_only:
        modules_files += [modules_dir / "vanilla-game.nro",
                          modules_dir / "newersmbw-game.nro", modules_dir / "newersmbw.romfs.zst",
                          modules_dir / "newersmbw-banner.png"]
        if not args.skip_symbols:
            modules_files += [modules_dir / "vanilla-debug-symbols.zip",
                              modules_dir / "newersmbw-debug-symbols.zip"]
        for mod in mods:
            mod_assets = mod["assets"]
            modules_files += [modules_dir / mod_assets["nro"], modules_dir / mod_assets["romfs"],
                              modules_dir / mod_assets["banner"]]
            if not args.skip_symbols:
                modules_files.append(modules_dir / mod_assets["symbols"])

    manifest = {
        "tag": args.tag,
        "version": version,
        "main_repo": args.main_repo,
        "modules_repo": args.modules_repo,
        "launcher_only": args.launcher_only,
        "main": [file_record(path) for path in main_files],
        "modules": [file_record(path) for path in modules_files],
        "catalogue": assets,
    }
    (args.out / "release.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                           encoding="utf-8")

    gh_script(args.out / "upload-main.sh", args.main_repo, args.tag, main_files,
              "NSMBW Switch launcher, base runtime and catalogue")
    modules_notes = ("NSMBW Switch launcher" if args.launcher_only else
                     "NSMBW Switch runtimes, resources and debug symbols")
    gh_script(args.out / "upload-modules.sh", args.modules_repo, args.tag, modules_files,
              modules_notes, create_repo=True)

    print("version    %s" % version)
    print("catalogue  %s" % (main_dir / "assets.json"))
    print("zip        %s" % zip_path)
    for path in main_files + modules_files:
        print("asset      %s  %d bytes" % (path, path.stat().st_size))
    print("manifest   %s" % (args.out / "release.json"))
    print("upload     %s" % (args.out / "upload-main.sh"))
    print("upload     %s" % (args.out / "upload-modules.sh"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)
