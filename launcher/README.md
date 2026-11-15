# NSMBW Launcher

Separate NRO that lists installed games under `/switch/nsmbw/games/` and chainloads them with
`envSetNextLoad`. No game runtime code is linked in; the UI is vendored from
[Sphaira](https://github.com/TotalJustice/sphaira) (GPLv3).

## Build

```
export DEVKITPRO=/opt/devkitpro
cmake -S launcher -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-launcher
```

## PC tests

A headless Linux build of the portable launcher logic lives in `launcher/tests/pc`. It is a test
harness, not a PC launcher:

```
cmake -S launcher/tests/pc -B build-launcher-pc -G Ninja
cmake --build build-launcher-pc
cd build-launcher-pc && ctest --output-on-failure
```

See `launcher/tests/pc/README.md` for coverage, screenshots and limitations.

## Generated translation folders

The base game and Newer use separate translator output folders: `generated/` for the base game and
`generated-newer/` for Newer. Configure the runtime build with the matching folder:

```
cmake -S runtime -B build-switch-product -DMKW_GENERATED_DIR="$PWD/generated" ...
cmake -S runtime -B build-switch-newer -DMKW_GENERATED_DIR="$PWD/generated-newer" ...
```

`projects/nsmbw/tools/build-linux.sh` selects the folder automatically.

## Layout

```
/switch/nsmbw/
  launcher.nro
  assets.json
  assets.url          (optional catalogue URL override)
  assets.local.json   (optional local catalogue override)
  assets.cache.json   (last downloaded catalogue)
  usage.json          (launch order for the main screen)
  cacert.pem          (written from the embedded bundle)
  banners/<id>.png
  discs/
    SMNE01/          (extracted base game, shared by the base game and every mod)
      files/
      sys/
  games/
    vanilla/
      game.nro
      data/          (runtime user data, created by the game)
    newersmbw/
      game.nro
      assets.romfs   (zstd-decompressed RomFS image)
      data/
      installed.json
```

A game folder without `installed.json` is a manual install. Its version is read from the NRO's NACP,
it is launched as-is, and it becomes managed the first time it is updated. `installed.json` records
each component's version, sha256 and size; a size mismatch marks the game as Update so Reinstall is
offered.

The base game data is extracted once into `discs/SMNE01/` and shared by the base game and every
mod. Mods do not copy it: they only need their own NRO plus their RomFS image. Newer stores all of
its settings, saves, caches and logs directly in `games/newersmbw/data/`.

The launcher looks for a disc image next to `launcher.nro`, accepts only the USA SMNE01 revision 1
disc, and extracts it automatically into `discs/SMNE01/` on the next start. A preassembled Newer
disc image is not used; Newer is installed as an overlay from the catalogue.

## assets.json

The launcher starts offline. It loads `<launcher root>/assets.local.json`, then
`assets.cache.json`, then the `assets.json` shipped in the release ZIP. No network call is made at
startup.

The Download games page always fetches the catalogue from `assets.url` (or the bundled URL) behind
a progress dialog. It does not use `assets.local.json`, `assets.cache.json` or the bundled
catalogue: if the fetch fails, the page shows the error and returns to the game list. A successful
fetch is cached as `assets.cache.json` for the next offline start. Settings can clear the local
catalogue. `Check launcher update`, `Update all` and per-game `Check for updates` also refresh the
catalogue before reporting.

```json
{
  "schema": 1,
  "launcher": { "version": "1.0.0", "url": "...", "sha256": "...", "size": 0 },
  "games": {
    "vanilla": {
      "name": "New Super Mario Bros. Wii",
      "author": "Nintendo",
      "components": {
        "nro": { "version": "1.0.0", "url": ".../NSMBW.nro", "sha256": "...", "size": 0 }
      }
    },
    "newersmbw": {
      "name": "Newer Super Mario Bros. Wii",
      "author": "Newer Team",
      "components": {
        "nro": { "version": "1.30", "url": ".../newersmbw-game.nro", "sha256": "...", "size": 0 },
        "romfs": {
          "version": "1.30",
          "url": ".../newersmbw.romfs.zst",
          "sha256": "<compressed>",
          "size": 0,
          "image_sha256": "<uncompressed>",
          "image_size": 0
        }
      },
      "banner": {
        "url": ".../newersmbw-banner.png",
        "sha256": "...",
        "size": 0
      }
    }
  }
}
```

Components may use `url` (HTTP) or `path` (SD card). Each component has its own version, so an NRO
bump with an unchanged RomFS downloads only the NRO. `vanilla` has no RomFS component: it lets the
launcher deliver runtime updates without a new ZIP.

The `banner` is optional. It is not versioned; the launcher replaces the cached image when the
catalogue checksum changes. Without a banner, or offline before the first download, the row shows
the embedded NSMBW logo.

Use `tools/make_assets.py` to fill in the hashes from the built files:

```
python3 launcher/tools/make_assets.py release.json -o launcher/assets.json
```

See the script's docstring for the small release spec.

## Local release testing

`tools/mock_release.py` serves the staged release folders as a fake GitHub release over HTTP. It
symlinks the files into `/tmp/opencode/mock-release`, rewrites every catalogue URL to the local
address while keeping the checksums, and prints the `assets.url` content to upload. This is for
development only; the committed catalogue keeps the real GitHub URLs.

```
python3 launcher/tools/mock_release.py
```

Then upload `/switch/nsmbw/assets.url` with the printed URL, remove `assets.local.json` and
`assets.cache.json`, and open Download games. The server logs every request.

If the console cannot reach the dev machine directly, expose the mock with a Cloudflare quick
tunnel and pass its public URL:

```
cloudflared tunnel --url http://127.0.0.1:8000 --no-autoupdate --metrics 127.0.0.1:20242
python3 launcher/tools/mock_release.py --port 8000 --base-url https://<name>.trycloudflare.com
```

The tunnel URL is temporary. Update `/switch/nsmbw/assets.url` when it changes.

If Tailscale owns the route to the console, `launcher/tools/use-lan-mock.sh` adds a `/32` LAN
route, moves `assets.local.json` aside and points `assets.url` at the LAN mock. It asks for sudo.

## Screens

The main screen lists installed games with large banner rows, sorted by the last time they were
launched. Launch order is stored in `<launcher root>/usage.json`. The bottom row is a separate
`Download games` entry with the number of catalogue games that are not installed. Game rows show the
author under the title. The download page and update checks all refresh the online catalogue.

The download screen lists catalogue games that are not installed, using the original 80 px rows.
Opening it fetches the catalogue behind a progress dialog. If the catalogue cannot be loaded, the
page shows the error and returns to the game list. Selecting a game shows its download size, the
free-space requirement and the available space, then installs it and records it as recently used,
so it moves to the top of the main screen. `B` returns to the game list.

## Controls

- A: launch, install, update or open the download page.
- B: quit from the game list, back from the download list.
- X: Game options when an installed game is selected (Update or Check for updates, Reinstall,
  Verify files, Delete game, Delete game and save data).
- Minus: Settings (Update all, Check launcher update, Clear local catalogue, About).

`Delete game` removes the NRO, RomFS and install metadata but keeps `data/`, settings and disc
images. `Delete game and save data` removes the whole game folder. The base disc lives in
`discs/SMNE01/`, so both variants are available for every game. Update, Reinstall and Update all
show the download size and free-space requirement before starting.

## RomFS pack

`tools/pack_romfs.py <pack folder> assets.romfs` runs `build_romfs`, compresses with `zstd`, and
prints the `romfs` component fields. The pack folder holds `riivolution/NewerSMBW.xml` and
`NewerSMBW/`.

The launcher streams the `.zst` through `ZSTD_decompressStream` into `assets.romfs`, hashing both
streams, then verifies `sha256`, `image_sha256` and `image_size`. The game mounts the image as
`pak:/` and applies the Riivolution XML from it. The old `.part` and `.removed` leftovers are
deleted at startup.

## Updates

Per-game Update installs only stale components. Reinstall forces both. Check for updates refreshes
the catalogue and offers the update when one is available. Update all refreshes first, then
processes every stale entry in one progress window. Results are shown in dialogs. The launcher
update downloads to `launcher.nro.new`, checks the NRO magic and NACP version, backs up the current
file, copies the new one over it and restarts the launcher.

## Caches

The Newer build bundles its own Aurora pipeline cache (`newer_pipeline_cache.db`). Dawn's shader
cache is not bundled: on the first Newer launch, if
`games/vanilla/data/Cache/dawn_cache.db` exists, it is copied into the Newer data folder before the
renderer opens it, which skips most of the first shader compile.

## Offline install

The release ZIP mirrors the SD layout (`switch/nsmbw/...`). For a local RomFS pack, copy the
`.zst` somewhere on the SD card and point the catalog `romfs.path` at it; the launcher decompresses
it on install. No zstd tool is needed on the console. A local `banner.path` works the same way.
`make_assets.py` refuses `path` entries unless `--allow-local-paths` is passed, so a release
catalogue cannot accidentally contain a dev SD path.

## Network

GitHub is fetched over verified TLS using the root bundle embedded in the launcher, written to
`/switch/nsmbw/cacert.pem` on first use. Catalogue responses are capped at 4 MB and banners at
8 MB.

## Release assets

The main repository release carries `launcher.nro`, `assets.json`, `NSMBW.nro`, `NSMBW-switch.zip`
and `debug-symbols.zip`. The standalone `NSMBW.nro` is what the `vanilla` catalogue entry updates.

The mods repository release carries `newersmbw-game.nro`, `newersmbw.romfs.zst`,
`newersmbw-banner.png` and the Newer debug symbols. The paths in the example catalogue match those
names.

## Versions

`LAUNCHER_VERSION` comes from the `project(... VERSION ...)` line in `CMakeLists.txt` and is also
written into the NACP. Bump it and the `launcher.version` field together, otherwise the launcher
update row will not appear. The runtime NACP version is `1.0.0` for the base game and `1.30` for
Newer, so manual installs report the same version as the catalogue. Bump the base NACP version and
the `vanilla` catalogue entry together when a runtime update should reach existing installs.

The launcher disables console auto-sleep while it is open, so long downloads and extractions are not
interrupted by the idle timer.
