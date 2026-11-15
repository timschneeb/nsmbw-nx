#!/usr/bin/env python3
"""Build assets.json from a release spec and the built files.

The spec mirrors assets.json but names local files instead of hashes:

{
  "launcher": {
    "version": "1.0.0",
    "file": "build-launcher/launcher.nro",
    "url": "https://github.com/timschneeb/nsmbw-nx/releases/latest/download/launcher.nro"
  },
  "games": {
    "vanilla": {
      "name": "New Super Mario Bros. Wii",
      "author": "Nintendo",
      "nro": {
        "version": "1.0.0",
        "file": "build-switch-product/NSMBW.nro",
        "url": ".../NSMBW.nro"
      }
    },
    "newersmbw": {
      "name": "Newer Super Mario Bros. Wii",
      "author": "Newer Team",
      "nro": {"version": "1.30", "file": "...", "url": "..."},
      "romfs": {
        "version": "1.30",
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
    version = spec.get("version", "")
    if require_version and not version:
        raise ValueError("component is missing a version")
    if version:
        out["version"] = version

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("spec")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--allow-local-paths", action="store_true",
                        help="allow SD-card path entries in the generated catalogue")
    args = parser.parse_args()

    with open(args.spec, "r", encoding="utf-8") as handle:
        spec = json.load(handle)

    launcher = component(spec.get("launcher", {}))
    assets = {
        "schema": 1,
        "launcher": {
            "version": launcher.get("version", ""),
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
            game["components"]["romfs"] = component(game_spec["romfs"])
        if not game["components"]:
            raise ValueError("game %s has no components" % game_id)
        if "banner" in game_spec:
            game["banner"] = component(game_spec["banner"], require_version=False)
        assets["games"][game_id] = game

    if not args.allow_local_paths and contains_key(assets, "path"):
        raise ValueError("catalogue contains a local path entry; use --allow-local-paths for a test catalogue")

    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(assets, handle, indent=2)
        handle.write("\n")

    print("wrote %s: %d game(s)" % (args.output, len(assets["games"])))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)
