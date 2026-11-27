# Building New Super Mario Bros. Wii from source

Building it yourself still needs your own **New Super Mario Bros. Wii (USA, SMNE01, revision 1)**,
extracted with Dolphin's **Extract Entire Disc**. These are the steps and scripts the 0.0.1 release
was built with, on Windows 10 x64.

The PowerShell build scripts in [`tools/`](tools) find everything from the repository's own location
([`tools/paths.ps1`](tools/paths.ps1)):

- The build folders (`native-build`, `native-build-newer`, `android-build`) are in the repository, with
  their logs in each one's `logs` folder (`NSMBW_LOGS` overrides that).
- The toolchain (`llvm-mingw\`, `CMake\`, `Ninja\`, optionally `dotnet\`) is `..\toolchain` next to the
  repository if there is one, else `Launcher\artifacts\portable-tools`, where step 1 puts it
  (`NSMBW_TOOLCHAIN` overrides it). Without a `dotnet\` there, the .NET SDK on `PATH` is used.
- The Android NDK is `ANDROID_NDK_HOME`, else `ndk\29.0.14206865` in `ANDROID_HOME` or in Android
  Studio's `%LOCALAPPDATA%\Android\Sdk`.
- The translator's inputs are `..\game` next to the repository (step 2).

## 1. Tools

- The same native toolchain as WiiCompiled: .NET 8 SDK, CMake, Ninja and llvm-mingw 20260616.
  [`Launcher/Prepare-PortableTools.ps1`](../../Launcher/Prepare-PortableTools.ps1) downloads the
  pinned versions.
- The pinned third-party sources in `Dependencies/`:
  [`Launcher/Prepare-Dependencies.ps1`](../../Launcher/Prepare-Dependencies.ps1) `-Destination Dependencies`
  (without it they go to `Launcher\artifacts\dependencies`, for the installer). Android also needs
  `Dependencies/dawn_prebuilt_android` (Dawn `v20260603.191052`, `dawn-android-aarch64`).
- Python 3 for the scripts in `tools/` (standard library only).
- Android only: Android SDK (platform 34, build-tools 34), NDK 29.0.14206865, JDK 17, Gradle 8.7.

## 2. The game's code

```
python projects/nsmbw/tools/unpack_game.py <your extracted game folder>
python projects/nsmbw/tools/prelink_rels.py
```

`unpack_game.py` checks the disc (SMNE01 revision 1, clean `main.dol`), copies `sys/main.dol` and
unpacks the four boot RELs (`files/rels/*.rel.LZ`) into `..\game`. `prelink_rels.py` links the RELs
offline the way the game links them at boot, into the one `nsmbw_linked.rel` the translator reads
([`recomp.yml`](recomp.yml)). It also applies [`p1_character.py`](tools/p1_character.py) (mkwcat's "Player
1 Can Change Character" code) to it and to `main_patched.dol`, the `main.dol` the translator reads.

## 3. Translate

```
dotnet build translator/Translator.sln -c Release
```

Then the four translator steps, as [`tools/rebuild-all.ps1`](tools/rebuild-all.ps1) runs them:
`translate-recursive`, `emit-base-manifest`, `generate-data-init` and `emit-build-shards`. The base
game writes to `generated/` and Newer to `generated-newer/` (both ignored by git); keep them apart
so a Newer build cannot leak into the base NRO.

## 4. Windows

```
projects/nsmbw/tools/configure-native.ps1
projects/nsmbw/tools/build-native.ps1
```

The game is `native-build/WiiCompiled.exe`, next to the DLLs and data files it needs. Name your
extracted game folder `game` and put it next to the exe, or let the exe ask for it.

## 5. Android

```
projects/nsmbw/tools/configure-android.ps1
projects/nsmbw/tools/build-android.ps1
projects/nsmbw/tools/stage-android.ps1
gradle -p android assembleRelease
```

`build-android.ps1` makes `android-build/libmain.so`; `stage-android.ps1` strips it and copies it,
with the runtime's data files, into the app (`android/native`, ignored by git). The release APK is
signed with the build machine's Android debug key (see `android/app/build.gradle`).
`deploy-android.ps1` builds, stages and packages a debug APK and installs it on a connected phone.

## 6. Linux

Steps 2 and 3 are the same on Linux; [`tools/build-linux.sh`](tools/build-linux.sh) runs step 3 and the
native build in one go. It needs:

- The .NET 8 SDK, on `PATH` or in `~/.dotnet` (no root needed:
  `dotnet-install.sh --channel 8.0 --install-dir ~/.dotnet`).
- The pinned clang, CMake and Ninja:
  `Launcher/prepare-portable-tools.sh --arch x86_64` puts them in
  `Launcher/artifacts/portable-tools/toolchain-x86_64` (about 2 GB to download, 556 MB kept).
- Python 3 and the X11, ALSA and Vulkan development headers (on Fedora `libX11-devel`, `alsa-lib-devel`,
  `vulkan-loader-devel`). Native Wayland, PipeWire and PulseAudio need `wayland-devel`,
  `libxkbcommon-devel`, `libdecor-devel`, `pipewire-devel` and `pulseaudio-libs-devel`; without them the
  game runs through XWayland and ALSA. The script switches off SDL's XScrnSaver and XTest support when
  their headers (`libXScrnSaver-devel`, `libXtst-devel`) are missing.

```
python3 projects/nsmbw/tools/unpack_game.py <your extracted game folder>
python3 projects/nsmbw/tools/prelink_rels.py
projects/nsmbw/tools/build-linux.sh --game <your extracted game folder>
```

The game is `../nsmbw-local-build/WiiCompiled`, with a `game` link to your extracted game beside it.
SDL3, Dawn and the other third-party sources are fetched by CMake on the first configure.

### Italian language-pack layouts

The USA `SMNE01` executable does not include Italian. You can use the Italian files from your own
NSMBW EU disc:

1. Extract the EU disc with Dolphin's **Extract Entire Disc**. The language files are in
   `files/EU/ItaEU/`.
2. Locate the extracted USA game's `DATA` directory (the directory set as `paths.dvd_root` in
   `Config.toml`).
3. Copy the **contents** of the EU disc's `files/EU/ItaEU/` into
   `<DATA>/files/EU/ItaEU/`, keeping the directory structure. For example, if the USA game is at
   `~/Games/NSMBW/DATA`, the Italian message archive should be at
   `~/Games/NSMBW/DATA/files/EU/ItaEU/Message/Message.arc`. Do not add another `ItaEU/` directory.
4. Start the game, open **Settings > Language**, select **Italiano**, and restart the game.

The runtime overlays the pack on the USA English files; only files present in the USA game are
used. Because the EU layouts refer to differently named resources, the runtime adapts the internal
resource names in `wiiStrap.arc`, `corseClear.arc` and `gameOver.arc` at startup. The originals in
`DATA/files/EU/ItaEU/` are not modified; adapted copies are cached under `Cache/LanguageLayouts`
beside `Config.toml`. Existing installations can still use `Languages/it/` beside `Config.toml`.

## 7. Newer Super Mario Bros. Wii

[Newer Super Mario Bros. Wii](https://newerteam.com/wii/) runs on top of the USA game. Its disc
(ID `SMNE03`) keeps the four RELs as they are, patches `main.dol` with a small loader, and loads its
own PowerPC code at boot: `files/NewerRes/SystemUS_1.bin` (raw patches), `DLCodeUS_1.bin` (the code)
and `DLRelocsUS_1.bin` (its relocations and its hooks into the game). A static recompilation never
runs that loader's result, so [`tools/newer_prelink.py`](tools/newer_prelink.py) does its work offline,
the way `prelink_rels.py` links the RELs, and the translator translates the patched game and Newer's
code together. The loader still runs in the game, so its data ends up in memory as on a Wii.

Build the base game's `../game` first (steps 2 and 6), then, with Newer extracted by Dolphin:

```
projects/nsmbw/tools/build-linux.sh --newer <extracted Newer folder> --game <extracted Newer folder>
```

That runs `newer_prelink.py` (it writes `../game-newer`: the patched `main.dol`, the synthetic REL
with Newer's code appended, and `MAP.txt` with Newer's function starts), translates
[`recomp-newer.yml`](recomp-newer.yml) and builds `native-build-newer` with `-DNSMBW_GAME_ID=SMNE03`,
so the game accepts the Newer disc. The game is `../nsmbw-newer-build/WiiCompiled`.

On Windows, with the step 4 scripts (the same command the first time and whenever something changed):

```
projects/nsmbw/tools/rebuild-all.ps1 -Newer <extracted Newer folder>
```

`rebuild-all.ps1 -Newer` runs `newer_prelink.py`, translates `recomp-newer.yml` and builds
`native-build-newer`. The first time it also configures that folder (`configure-native.ps1 -Newer`,
with `-DNSMBW_GAME_ID=SMNE03`), which CMake can only do once the translation is there;
`build-native.ps1 -Newer` only builds it. The game is `native-build-newer/WiiCompiled.exe`.

By hand, on any platform: run `newer_prelink.py <extracted Newer folder>`, then the step 3 commands
with `--project projects/nsmbw/recomp-newer.yml`, and configure with `-DNSMBW_GAME_ID=SMNE03`.

### From the Riivolution pack (no Newer disc)

The official Riivolution pack has no patched main.dol: it writes `Loader.bin` to `0x80001800` and
applies memory patches at boot. `tools/newer_pack_prelink.py` does that offline instead:

```
python3 projects/nsmbw/tools/newer_pack_prelink.py <pack folder>
```

The pack folder holds `riivolution/NewerSMBW.xml` and `NewerSMBW/` (or `files/`). The script applies
the pack's `<memory>` patches whose `original` bytes match the clean USA rev 1 main.dol (the other
region entries are skipped), adds `Loader.bin` as a third text section at `0x80001800`, keeps the
phase slot pointing there, then runs `newer_prelink.py` for the REL, Newer's blob and the function
map. The loader stays position independent in the low-memory layout the pack expects.

Translate [`recomp-newer-pack.yml`](recomp-newer-pack.yml) (same as `recomp-newer.yml` plus
`0x80001800` in the entry points) with the step 3 commands, and configure with
`-DNSMBW_GAME_ID=SMNE03`. The loader and the blob are translated like the game's own code; the
loader still runs at boot and loads the pack's `NewerRes` files over the overlay.

### Newer Holiday Special (pack mod)

Holiday Special is a pack over the vanilla USA rev 1 disc with its own small DLCode, not a full
Newer build. Extract the official pack, then prelink it with the same tool:

```
python3 projects/nsmbw/tools/newer_pack_prelink.py _references/Newer_W_Holiday_Special_1.12 \
    --pack-folder XmasNewer --xml riivolution/XmasNewer.xml --no-newer-fixes
```

`--no-newer-fixes` skips Newer 1.30's replay-heap and DVDDiskCheck patches, which the Holiday pack
does not need. `--dlcode-addr` defaults to `0x80DF8FC0`, the address the loader's allocation lands
at. If the loader logs another `[OSReport] DLCode @ ...` address, pass the measured value. A pack
whose DLCode is much smaller can instead be padded to Newer's 463,252-byte allocation size in the
RomFS copy, which keeps the loader on the proven `0x80DF8FC0`; the manifest's `pad_size` records
which choice was used. Holiday's 1,860-byte DLCode was measured at `0x80DF8FC0` in the Switch
build, so it uses the original file and no padding.

Translate [`recomp-holiday.yml`](recomp-holiday.yml) into `generated-holiday/`, then configure the
Switch build with `-DNSMBW_PRODUCT=holiday -DNSMBW_GAME_ID=SMNE01
-DMKW_GENERATED_DIR=<repo>/generated-holiday`. The runtime's product descriptor is
`src/product/base_product.cpp`; it names the pack folder (`XmasNewer`), version file, save
(`XmasNer.sav`) and desktop game folder (`game-holiday`).

Holiday ships `runtime/assets/pipeline/newer_holiday_pipeline_cache.db`: the vanilla base plus the
shaders collected on the console. Merge a console cache with:

```
python3 projects/nsmbw/tools/make_pipeline_seed.py \
    runtime/assets/pipeline/newer_holiday_pipeline_cache.db \
    runtime/assets/pipeline/initial_pipeline_cache.db <collected Cache/pipeline_cache.db>
```

On first boot it also copies the vanilla `games/vanilla/data/Cache/dawn_cache.db` into its own
cache. The forced shader prewarm screen still runs on first boot.

The launcher entry is generated from [`mods/holiday.json`](mods/holiday.json) by
`launcher/tools/package_release.py`. Pack the launcher's RomFS from the extracted pack:

```
python3 launcher/tools/pack_romfs.py _references/Newer_W_Holiday_Special_1.12 \
    /tmp/newersmbw-holiday.romfs --keep
```

### Switch: RomFS pack and shared base

For the launcher, the pack folder is packed as one Nintendo RomFS image and zstd-compressed:

```
python3 launcher/tools/pack_romfs.py _references/Newer_W_1.30 /tmp/newersmbw.romfs
```

The launcher decompresses the `.zst` on install to `games/newersmbw/assets.romfs`; the runtime
mounts it as `pak:/` and adds it as the Riivolution overlay root. Newer does not need a
`game-newer` folder: it reads the base game's extracted folder from `games/vanilla/game`, and
`Check()` accepts the SMNE01 header when the pack carries `NewerRes/DLCodeUS_1.bin` (463,252
bytes). The launcher never copies the base tree.

The Newer build ships its own Aurora pipeline cache (`assets/pipeline/newer_pipeline_cache.db`,
selected by `NSMBW_PRODUCT=newer`). On first launch the runtime also copies the vanilla Dawn cache
(`games/vanilla/data/Cache/dawn_cache.db`) if present, which removes most of the initial shader
compile. Dawn cache entries are keyed by shader hashes, so the shared cache is safe.

The Switch build writes `VERSION` into each game NRO's NACP. The launcher catalogue carries the
game versions (`1.0.0`, `1.30`, `1.12`) for updates; it is generated from the built files and the
mod manifests with `launcher/tools/package_release.py`, and its mod entries point at the modules
release with their banners.

The base game, Newer and Holiday Special use separate translation folders, `generated/`,
`generated-newer/` and `generated-holiday/`. Configure each runtime build with
`-DMKW_GENERATED_DIR=<repo>/generated...`; the first build after a switch recompiles most shards.

Things that are specific to Newer:

- Newer's code is placed where its loader's heap allocation lands at boot, `DLCODE_ADDR` in
  `newer_prelink.py` (0x80DF8FC0). The game logs the real address (`[OSReport] DLCode @ 0x80df8fc0`);
  if a runtime change ever moves it, update the constant, or Newer's first hook jumps into the wrong
  place.
- Hooks that call Newer code with `bl` but whose code returns for the hooked function are rewritten as
  `b` by `newer_prelink.py` (`rewritten_hooks` in `../game-newer/nsmbw_rel_layout.json`).
- Newer v1.30 is the version this was made with. Another version needs a new `DLCODE_ADDR` and may hook
  functions the runtime replaces natively; `newer_prelink.py` checks the files it expects.
- With `NSMBW_GAME_ID=SMNE03` the game calls itself Newer Super Mario Bros. Wii and accepts only Newer
  v1.30 for the USA (NTSC). Every Newer version keeps the disc ID, so the version is told by the size of
  `files/NewerRes/DLCodeUS_1.bin`, Newer's code (`src/product/base_product.cpp`).
- Newer's program keeps its own settings, saves, logs and caches (`Documents\NewerWiiSaveData` on
  Windows, a `NewerWiiSaveData` folder inside the base game's data folder elsewhere), and the extracted
  game it finds by itself beside it is `game-newer`, not `game`. So both programs can share one folder
  (releases name them `NSMBW` and `NewerSMBW`).

## Other tools

- `make_pipeline_seed.py <out.db> <pipeline_cache.db> [...]` merges caches a console wrote
  (`Cache/pipeline_cache.db`, with its `-wal` file beside it) into the shipped
  `runtime/assets/pipeline/initial_pipeline_cache.db`, so a fresh install prewarms the shaders that
  console discovered. The first input seeds the output; later ones add their missing recipes.
- `build_function_map.py` made [`MAP.txt`](MAP.txt), the function map the translator follows.
- `ppcdis.py`, `symhash.py`, `nsmbw_versions.py`, `hook_size_check.py`, `verify_runtime_addrs.py`:
  the reverse-engineering helpers used to move WiiCompiled's Mario Kart Wii hooks to NSMBW.
- `capture-window.ps1`, `keyboard-run.ps1`, `capture-session.ps1`, `run-android.ps1`,
  `android-ui.ps1`, `gfx-test-android.ps1`, `deploy-android.ps1`: test drivers for the Windows and
  Android builds. They still expect the `E:\NSMBWPort` layout the release was built in.
