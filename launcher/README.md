# NSMBW Launcher

Separate NRO that lists installed games under `/switch/nsmbw/games/` and chainloads them with
`envSetNextLoad`. No game runtime code is linked in; the UI is vendored from
[Sphaira](https://github.com/TotalJustice/sphaira) (GPLv3).

## Build

```
export DEVKITPRO=/opt/devkitpro
cmake -S launcher -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DNX_UAM_EXE=/path/to/uam
cmake --build build-launcher
```

`uam` must be the deko3d shader compiler (`uam_tool`), not the unrelated `/usr/bin/uam`.

## Layout

```
/switch/nsmbw/
  launcher.nro
  assets.json
  games/
    vanilla/
      game.nro
      game/          (extracted disc, shared by mods)
      data/          (runtime user data, created by the game)
    newersmbw/
      game.nro
      assets.romfs   (zstd-decompressed RomFS image)
      data/NewerWiiSaveData/
      installed.json
```

A game folder without `installed.json` is a manual install. It is launched as-is and is never
overwritten unless the user picks Update.

Mods reuse the vanilla extracted disc folder (`games/vanilla/game`) as their DVD root. They do not
copy it: the launcher and runtime only need the mod's NRO plus its RomFS image.

## assets.json

Fetched from `ASSETS_URL` (`.../nsmbw-nx/releases/latest/download/assets.json`). If that fails, the
`assets.json` shipped in the release ZIP is used. `/switch/nsmbw/assets.local.json` overrides both,
for local testing.

```json
{
  "schema": 1,
  "launcher": { "version": "1.0.0", "url": "...", "sha256": "...", "size": 0 },
  "games": {
    "newersmbw": {
      "name": "Newer Super Mario Bros. Wii",
      "components": {
        "nro": { "version": "1.30", "url": ".../game.nro", "sha256": "...", "size": 0 },
        "romfs": {
          "version": "1.30",
          "url": ".../newersmbw.romfs.zst",
          "sha256": "<compressed>",
          "size": 0,
          "image_sha256": "<uncompressed>",
          "image_size": 0
        }
      }
    }
  }
}
```

Components may use `url` (HTTP) or `path` (SD card). Each component has its own version, so an NRO
bump with an unchanged RomFS downloads only the NRO.

## RomFS pack

`tools/pack_romfs.py <pack folder> assets.romfs` runs `build_romfs`, compresses with `zstd`, and
prints the `romfs` component fields. The pack folder holds `riivolution/NewerSMBW.xml` and
`NewerSMBW/`.

The launcher streams the `.zst` through `ZSTD_decompressStream` into `assets.romfs`, hashing both
streams, then verifies `sha256`, `image_sha256` and `image_size`. The game mounts the image as
`pak:/` and applies the Riivolution XML from it.

## Caches

The Newer build bundles its own Aurora pipeline cache (`newer_pipeline_cache.db`). Dawn's shader
cache is not bundled: on the first Newer launch, if
`games/vanilla/data/Cache/dawn_cache.db` exists, it is copied into the Newer data folder before the
renderer opens it, which skips most of the first shader compile.

## Offline install

The release ZIP mirrors the SD layout (`switch/nsmbw/...`). For a local RomFS pack, copy the
`.zst` somewhere on the SD card and point the catalog `romfs.path` at it; the launcher decompresses
it on install. No zstd tool is needed on the console.

## Versions

`LAUNCHER_VERSION` comes from the `project(... VERSION ...)` line in `CMakeLists.txt`. Bump it and
the `launcher.version` field together, otherwise the launcher update row will not appear.
