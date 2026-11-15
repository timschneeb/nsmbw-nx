#!/usr/bin/env python3
"""Build assets.json from a release spec and the built files.

The spec mirrors assets.json but names local files instead of hashes:

{
  "recomp_version": "0.0.1",
  "launcher": {
    "version": "0.0.1",
    "file": "build-launcher/launcher.nro",
    "url": "https://github.com/timschneeb/nsmbw-nx-modules/releases/latest/download/launcher.nro"
  },
  "games": {
    "vanilla": {
      "name": "New Super Mario Bros. Wii",
      "author": "Nintendo",
      "nro": {
        "recomp_version": "0.0.1",
        "game_version": "1.0.0",
        "region": "U",
        "file": "build-switch-product/NSMBW.nro",
        "url": ".../vanilla-game.nro"
      }
    },
    "newersmbw": {
      "name": "Newer Super Mario Bros. Wii",
      "author": "Newer Team",
      "nro": {"recomp_version": "0.0.1", "game_version": "1.30", "region": "U",
              "file": "...", "url": "..."},
      "romfs": {
        "game_version": "1.30",
        "region": "U",
        "file": "newersmbw.romfs.zst",
        "image": "newersmbw.romfs",
        "url": ".../newersmbw.romfs.zst"
      },
      "banner": {"file": "launcher/assets/banners/newersmbw.png", "url": ".../newersmbw-banner.png"}
    }
  }
}

A component may use "path" instead of "url" for a local install.
Run: python3 launcher/tools/make_assets.py release.json -o launcher/assets.json
"""

import argparse
import hashlib
import json
import os
import sys


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def component(spec, require_version=True):
    if not isinstance(spec, dict):
        raise ValueError("component must be an object")
    out = {}
    legacy_version = spec.get("version", "")
    recomp_version = spec.get("recomp_version", legacy_version)
    game_version = spec.get("game_version", legacy_version)
    region = spec.get("region", "")
    if require_version and not (recomp_version or game_version):
        raise ValueError("component is missing a version")
    if recomp_version:
        out["recomp_version"] = recomp_version
    if game_version:
        out["game_version"] = game_version
    if region:
        out["region"] = region

    source = spec.get("file")
    if source:
        if not os.path.isfile(source):
            raise ValueError("missing file: %s" % source)
        out["sha256"] = sha256(source)
        out["size"] = os.path.getsize(source)
    if spec.get("url"):
        out["url"] = spec["url"]
    if spec.get("path"):
        out["path"] = spec["path"]
    if not out.get("url") and not out.get("path"):
        raise ValueError("component has neither url nor path")

    image = spec.get("image")
    if image:
        if not os.path.isfile(image):
            raise ValueError("missing image: %s" % image)
        out["image_sha256"] = sha256(image)
        out["image_size"] = os.path.getsize(image)
    return out


def contains_key(node, key):
    if isinstance(node, dict):
        return key in node or any(contains_key(value, key) for value in node.values())
    if isinstance(node, list):
        return any(contains_key(value, key) for value in node)
    return False


def build_assets(spec, allow_local_paths=False):
    launcher_spec = spec.get("launcher", {})
    launcher = component(launcher_spec)
    assets = {
        "schema": 1,
        "recomp_version": spec.get("recomp_version", ""),
        "launcher": {
            "version": launcher_spec.get("version", ""),
            "url": launcher.get("url", ""),
            "sha256": launcher.get("sha256", ""),
            "size": launcher.get("size", 0),
        },
        "games": {},
    }

    for game_id, game_spec in spec.get("games", {}).items():
        game = {"name": game_spec.get("name", game_id), "components": {}}
        if game_spec.get("author"):
            game["author"] = game_spec["author"]
        if "nro" in game_spec:
            game["components"]["nro"] = component(game_spec["nro"])
        if "romfs" in game_spec:
            romfs = component(game_spec["romfs"])
            # A RomFS is a resource pack, not a recompiled NRO.
            romfs.pop("recomp_version", None)
            game["components"]["romfs"] = romfs
        if not game["components"]:
            raise ValueError("game %s has no components" % game_id)
        if "nro" in game["components"] and not game["components"]["nro"].get("recomp_version"):
            raise ValueError("game %s nro is missing recomp_version" % game_id)
        if "banner" in game_spec:
            game["banner"] = component(game_spec["banner"], require_version=False)
        assets["games"][game_id] = game

    if not allow_local_paths and contains_key(assets, "path"):
        raise ValueError("catalogue contains a local path entry; use --allow-local-paths for a test catalogue")
    return assets


def write_assets(assets, output):
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(assets, handle, indent=2)
        handle.write("\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("spec")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--allow-local-paths", action="store_true",
                        help="allow SD-card path entries in the generated catalogue")
    args = parser.parse_args()

    with open(args.spec, "r", encoding="utf-8") as handle:
        spec = json.load(handle)
    assets = build_assets(spec, args.allow_local_paths)
    write_assets(assets, args.output)
    print("wrote %s: %d game(s), recomp %s" % (
        args.output, len(assets["games"]), assets.get("recomp_version", "")))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)
