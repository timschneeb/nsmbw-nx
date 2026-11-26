# nsmbw-switch fork analysis

Analysis of `https://github.com/schuler-ph/nsmbw-switch`, cloned at
`_references/nsmbw-switch`, compared against upstream WitherBlitz and against our
Switch port. Implementation notes and measurements are recorded in
`NEWER_PERFORMANCE.md`.

## Base and scale

- Upstream base: `2ff64f5` (`origin/upstream-0.4.0`, `origin/pin`), upstream 0.4.0.
- Upstream merge: `e5fce0f`, upstream 0.5.0.
- Fork `main`: `3f59970`, 113 commits ahead of 0.5.0 (91 non-merge), all authored
  by schuler-ph.
- Diff against 0.5.0: 128 files, +18,137 / -967 lines.
- There are no translator changes. The recompiler and generated-code scheme are
  upstream's. The fork's work is the Switch platform layer, runtime, aurora and
  build scripts.
- The Switch build path is SMNE01-only: `build.sh` validates USA rev 1 and
  `projects/nsmbw/tools/build-switch.sh` hardcodes `NSMBW_GAME_ID=SMNE01`.
  The runtime still contains upstream's Newer product code, but the Switch
  scripts, water HLE and pipeline seed are vanilla-specific.
- Work is organized as topic branches (`nx-port-vtxcache`, `nx-port-pipeskip`,
  `nx-port-waterhle`, and so on). All performance branches are merged into
  `main` except `nx-port-pipeliste`, which only carries a larger pipeline seed
  database.

## Largest divergence: graphics stack

- Fork: Aurora -> Dawn with the deko3d backend -> libuam for GLSL to DKSH shader
  compilation.
- Ours: Aurora -> Dawn Vulkan -> Mesa NVK (Nouveau Tegra X1) -> Horizon.

They build Dawn from Google's tree at pin `13abc3b`, apply DriftDroid's deko3d
platform patch plus their own fixes, and disable Vulkan, GL, Metal, D3D, Null and
SPIR-V. The dependency script is
`projects/nsmbw/tools/build-switch-deps.sh`.

Consequences:

- Their submit path has no Vulkan loader, Dawn Vulkan command replay, or NVK.
  Their worker-side CPU cost is likely much lower than our 10.8 ms Dawn Vulkan
  submit.
- Their shader pipeline compiles GLSL through libuam, not Tint SPIR-V. They added
  a libuam memory fix because the first cache-miss boot ran out of heap.
- Our worker is currently 18.2 ms against a 23.4 ms guest, so switching backends
  would not raise fps today. It becomes interesting only after the guest drops
  near the worker.

## Optimizations and measured results

| Area | What the fork does | Fork measurement | Our state |
|---|---|---|---|
| Flat guest memory | 4 GiB flat window is the default; checked path behind `checked_memory.flag` or `NSMBW_CHECKED_MEMORY=1` | 1-1: game thread 25.4 to 19.4 ms (-24%), 31.6 to 38.4 fps (+22%) | Already flat, with a fixed x28 base and MMIO fault handling. Leaner than theirs. |
| GX thread | On by default on Switch by counting the process core mask, since libstdc++ reports 0 cores | Title demo 29.8 to 38.0 fps, level 45.3 to 46.2 fps | Already on via a Switch override. |
| `fifo::threaded()` | Inline in the header; wanted decision cached in plain bools | 1.1% of game thread before the fix | Ours is out-of-line with an atomic cache. Can be made inline. |
| GX raw path (`NSMBW_GXFAST`) | Skips the per-draw vertex republish, caches vertex size and first enabled attribute per generation, writes FIFO stores directly into the raw packet, skips timing for queued draws | About 17% of the game thread on 4-1's water before the fix | We already skip the republish and immediate-mode publish. We still lack the generation caches and direct gather. Our last stats-full 8-3 profile: `TryGetRawDirectFifoVertexSize` 0.97%, `HleFifoWrite` 2.4%, `RawBeginAppend` 0.75%. |
| Indexed billboards (`NSMBW_BEGINSKIP`, `NSMBW_RAWINDEX`) | Per-attribute GXBegins no longer publish twice; indexed POS/TEX/RGB8 draws take the raw path and expand indices at finish | Linux world map 3: game thread 7.20 to 6.07 ms (-15.7%) | We deliberately have no indexed raw path. Optional, scene-dependent. |
| Water HLE (`NSMBW_WATERHLE`) | Native `drawTexQuadManyTwinNormal` builds one GXBegin for about 1,100 quads instead of one per quad | About one fifth of the game thread in 4-1, two players | Ported and default on. The magma HLE was attempted but dropped: the guest win was real, but the below-surface gradient did not match. |
| Async texture copies | On by default on Switch; copies queue on the GX thread | 4-1 at 1785: game thread 12.2 to 12.4 -> 11.6 to 11.7 ms | Default is still off because `async_copies()` uses `std::thread::hardware_concurrency() >= 4`, which is 0 on Switch. |
| GX inline sync | Forced off on Switch; the game thread does not decode the tail itself | Their comment: the Switch game thread is the critical path | Ours uses `hardware_concurrency() < 4`, which is true on Switch, so inline decode is on. This is the clearest untested trap in our port. |
| Input path | Caches port to SDL gamepad and kind for 4 ms, drops on controller events; sets `SDL_HINT_INVALID_PARAM_CHECKS=1` | 1.4% plus 1.5% of game thread | Our Switch path still calls SDL per KPAD read, and we do not set the hint. |
| Logging | 256 KiB ring buffer plus logger thread; SD writes leave the game thread | About 20 ms per line, 9-17% of game thread | Already fully buffered with a background flusher. No action. |
| Helper thread placement | Wraps `__syscall_thread_create` and spreads helpers round-robin off the guest core; pipeline worker priority 0x3B, AX mix 0x2D, SDL audio 0x2B; 2 MiB stack floor | Fixed about 1,100 s prewarm and 25-220 ms game frames | We pin GX, frame and pipeline workers. General helper spread and audio priority are partly covered. |
| Pipeline skipping | `NSMBW_SKIPUNREADY=all` skips unready pipelines even in non-recurring EFB copy passes, with drop and stale-sample logging | Removed a 1.7 s first-visit stall in airship 4-37 | Dropped. It targets first-visit hitches, not steady frame time, and is not needed here. |
| Hot TLS | `MKW_HOT_TLS_ACCESSOR` inlines `mrs tpidrro_el0`, the thread pointer load and local-exec relocation | 47,945 call sites reduced to 616 | We still have 7,462 `bl __aarch64_read_tp` sites in `WiiCompiled.elf`. |
| Indirect dispatch memo | 1,024 entries, Fibonacci index from the high bits | Unquantified; fixed a 4-1 two-player hot spot | Ours is 512 entries with low-bit masking, but the shared fast raw table covers most calls. Low priority. |
| Code model | `-fPIE` objects plus hidden visibility for seven guest-memory tables | Removed about 195k GOT loads | We already build `-fPIE` and keep the flat base in x28. Ahead. |
| Shader memory | Drops aurora's shader module reference after each pipeline is built | Heap 0.4 to 2.7 GiB fixed | We already call `aurora_release_shader_modules()` after prewarm and under pressure. |
| Pipeline seed | Branch `nx-port-pipeliste` grows the seed cache from 1,816 to 2,107 rows via all 147 stages | First-visit compile reduction, not steady fps | Our vanilla cache has 2,310 rows and Newer 2,492. Ahead. |
| EFB snapshot (`nosnap`) | Clearing EFB copies read the EFB directly instead of copying the whole EFB first | "Most of the about 65 image copies per frame" in their GPU timing | Our `common.cpp` still always snapshots clearing copies. GPU-side, low priority since the GPU is not the wall. |
| Audio and disc | `NSMBW_REVERB_MT=1`, `NSMBW_FSCACHE=1` | Not quantified individually | We already have a native reverb worker and a read-through DVD cache. |
| Defaults | Sets measured switches on in `EarlyInit`: `GPU_TIMING=nosnap,gpucopy`, `GX_HANDOFF=overlap`, `GXFAST=1`, `FSCACHE=1`, `SKIPUNREADY=all`, `REVERB_MT=1`, `WATERHLE=1` | 59-60 fps at 1785/768/1600 in the measured levels | Our defaults differ, mostly because we already have equivalent paths. |

Other measured but experimental options, all off by default in the fork:
`NSMBW_GX_HANDOFF=nosealed` and `slots4`, `NSMBW_GX_DRAWDONE_ASYNC=1`,
`NSMBW_GX_COPYSRC_NOSYNC=1`, and `NSMBW_PIPELINE_DELAY_MS`. None are part of
their default configuration.

The remaining commits are build system (Docker one-command build, prebuilt
Dawn/libuam, disc image unpacking, BCA-check nop), Switch platform bring-up
(startup log, crash handler, applet check, NAND seeding, path handling, exit
behavior, NRO title/icon, 39-bit sphaira forwarder), controls and motion, and
diagnostics (PerfMonitor, dev link, sampler, input scripts, screenshots). They are
divergences from upstream, but not performance levers.

## Hardware concurrency audit

`aurora::hardware_thread_count()` in `aurora-main/lib/thread_utils.hpp` correctly
returns 3 on Switch. The problem is the direct `std::thread::hardware_concurrency()`
calls, because devkitA64's libstdc++ returns 0 unconditionally.

All direct uses outside `_references/` and build directories:

| Location | Use | Switch impact |
|---|---|---|
| `aurora-main/lib/gx/fifo.cpp:312` | `async_copies()` default `>= 4` | False on Switch. Texture copies stay on the game thread. Hot. |
| `aurora-main/lib/gx/fifo.cpp:343` | `inlineWhenIdle` default `< 4` | True on Switch. The game thread decodes the GX tail itself. Hot. |
| `aurora-main/lib/gx/fifo.cpp:276` | `threaded_for()` non-Switch fallback | Not used on Switch, which has an explicit `return true`. |
| `aurora-main/lib/thread_utils.hpp:19` | non-Switch fallback of `hardware_thread_count()` | Not used on Switch, which returns 3. |
| `runtime/src/settings_overlay.cpp:2649` | Core count for the shader-cache-reset CPU sampling loop | Rare startup path, not gameplay. Low impact, but should use `hardware_thread_count()` for consistency. |

Uses of `hardware_thread_count()` that are already correct on Switch:

- `aurora-main/lib/gfx/pipeline_cache.cpp:1389`, pipeline worker count.
- `aurora-main/lib/gx/frame_interpolation.cpp:95`, interpolation workers, which are
  off by default.
- `aurora-main/lib/gfx/texture_convert.cpp:274`, level-load texture decode workers,
  short-lived and not steady-state.

The first experiment should isolate the two hot uses. Environment-only A/B on
8-3, stats light:

1. Current: inline sync on, async copies off.
2. `NSMBW_GX_INLINE_SYNC=0`, async copies off.
3. Inline sync on, `NSMBW_ASYNC_COPY=1`.
4. `NSMBW_GX_INLINE_SYNC=0;NSMBW_ASYNC_COPY=1`.

Measure guest CPU, worker cycle, and p50. Once the winner is known, change the
Switch defaults in code so the result is not dependent on a per-run env list.

## HLE and Newer

The earlier statement that the water HLE is "vanilla-specific" needs a more
precise explanation.

The fork's `NSMBW_WATERHLE` is a native override of vanilla `0x8000D000`
(`d3d::rtexQuadManyTwin_c::drawTexQuadManyTwin`). The dispatcher reads the object
kind at `+52884` and calls the matching drawing function:

- `0x8000B530` normal water
- `0x8000BA70` magma
- `0x8000C020` sand
- `0x8000C5C0` magma high
- `0x8000CA50` poison

The fork implements the native fast path only for normal water. Lava, poison, sand
and magma still call their translated functions. That is why the default was
described as untested for those courses.

Newer does not change this code. The translated bodies for `func_8000D000`,
`func_8000B530` and `func_8000BA70` are byte-identical between our `generated/`
and `generated-newer/` trees. The dispatcher and the magma draw are at the same
addresses and have the same structure, including the quad loop and the
`GX_HLE_FIFO_WriteBurst` calls.

So the correct statements are:

- The fork's water HLE could run in Newer, because the function and its struct
  offsets are the same.
- It would only optimize normal water. Newer 8-3 uses the magma kind, which the
  fork left translated.
- A native magma HLE is feasible with the same technique, and one was implemented.
  It cut guest CPU by about 1.9 ms, but the below-surface gradient did not match
  and the worker became the wall, so it was dropped.
- HLE is not inherently vanilla-only. Generic engine HLEs (GX, math, audio, DVD)
  already work in both games. Game-specific HLEs work in both when the guest
  function and its data layout are the same, but each one needs validation.

Potential gain in 8-3: the translated magma function is 1.1% of the wall profile,
and its per-vertex FIFO writes are part of the 3.7 ms `gx fifo` figure, not all
attributed to the function itself. A magma HLE could remove the per-quad GXBegin
and most per-component FIFO traffic, so the real gain is larger than 1.1% but not
yet measured. The fork's normal-water result (about one fifth of the game thread
in 4-1 with two players) shows the pattern can be large when the scene is dominated
by that surface. A magma HLE is therefore a legitimate candidate for 8-3, but it is
a focused, game-specific project with visual validation, not a generic renderer
optimization.

## Recommendations and ordering

1. Run the four hardware-concurrency combinations on 8-3, then make the winning
   `inline sync` and `async copies` defaults Switch-specific in code.
2. Port the input cache and `SDL_HINT_INVALID_PARAM_CHECKS=1`. Expected from their
   numbers: about 0.5 to 0.7 ms of guest time.
3. Port the missing GXFAST pieces: per-generation vertex-size cache,
   first-enabled-attribute cache, direct FIFO packet stores, and no timer for
   queued raw draws. Our profile suggests about 0.5 to 0.8 ms.
4. Inline `threaded()` in `fifo.hpp`, keeping the atomic slow path.
5. Add hot TLS accessors for `g_guestThreadState` and the dispatch memo.
6. Investigate a magma HLE for 8-3 separately, with deterministic captures and a
   clear A/B. Treat it as a focused experiment, not a generic optimization.

Not worth taking now:

- The deko3d backend. It may lower the worker well below 18 ms, but the worker is
  not the wall. Revisit only after the guest is below about 16 ms.
- `nosnap` and `gpucopy`. GPU-side copies, and our GPU is at about 12.4 ms total
  in 8-3, well below the frame time.
- The expanded pipeline cache. Ours is already larger.
- Shader module release. Ours already does it.
- `NSMBW_RAWINDEX` for 8-3. It helped world map snowfall, not our current profile.

## Stock-clock reality

The fork's headline 59-60 fps is at 1785 MHz. Its README says stock docked was
about 43 fps and 72% game speed, measured before later speed-ups and not
remeasured since. It also has no Newer support and no PGO.

Our current 8-3 state is guest 23.4 ms, worker 18.2 ms, p50 about 25.4 to 27.2 ms.
If all the guest-side items above land, the realistic range is roughly 1.5 to
2.5 ms off the guest, taking 8-3 to about 21 to 22 ms, or roughly 45 to 47 fps.
That is consistent with the earlier small-item estimate, not with 60. PGO remains
the only measured lever of the needed size, and the fork does not have an
equivalent.

## Implementation log

- Step 1, hardware concurrency: Switch now defaults `async_copies` on and
  `inlineWhenIdle` off. Build `839e6cbdae52` measured on 8-3: guest 23.4 to
  21.3-21.6 ms (median 21.4), worker unchanged at 18.2 ms, p50 25.4-27.2 to
  23.3-24.0 ms (median 23.8). Keep.
- Step 2, input path: port gamepad/kind cache plus
  `SDL_HINT_INVALID_PARAM_CHECKS=1`. Build `f246e54fad0a` measured on 8-3: guest
  21.4 to 20.3 ms (median), p50 23.8 to 23.0 ms, worker 18.2 to 18.4 ms. Keep.
- Step 3, GXFAST: per-generation first-attribute and vertex-size caches, and
  direct stores into the open raw packet. `NSMBW_GX_RAW_FAST=0` restores the old
  path. Build `4f5c5ed9` measured on 8-3: guest 20.3 to 19.9 ms, p50 23.0 to
  22.8 ms, GX FIFO 3.77 to 3.44-3.68 ms. Keep.
- Step 4, inline `threaded()`, and step 5, hot TLS: the raw `threaded()` call is
  gone from the setters, and `__aarch64_read_tp` call sites dropped from 7,462
  to 829. Build `f5d4277c` measured on 8-3: guest 19.9 to 19.6 ms, p50 22.8 to
  22.7 ms. Keep.
- Step 6, indexed billboards: RAWINDEX takes indexed POS/TEX/colour draws
  through the raw path. Measured on 8-3: guest 19.6 to 19.3 ms, GX FIFO 3.5 to
  3.0 ms. General, not 8-3-specific. Keep.
- Step 7, water HLE: `runtime/src/hle/nsmbw_water_hle.cpp` now has the fork's
  validated normal-water path. Vanilla 4-1 at stock clocks measured a steady
  20.0-20.4 ms versus 20.1-20.9 ms for the opts build, about 0.2-0.4 ms. Both
  generated trees skip 0x8000D000 so the native dispatcher is the only
  definition. The normal path is on by default; `NSMBW_WATERHLE=0` restores the
  translated path.
- The magma HLE was attempted and dropped. It cut guest CPU from 19.3 to
  17.4 ms, but the below-surface gradient still did not match the translated
  path, and p50 was flat because the worker became the wall. The translated
  magma, poison and sand paths remain in the dispatcher.
