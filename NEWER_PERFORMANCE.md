# Newer SMBW performance

PC profiling notes for Newer Super Mario Bros. Wii on the NSMBW runtime. The goal is to find the
guest-thread cost behind the slow later levels and fix the largest items.

## Setup

- Build: `build-linux/WiiCompiled`, Release, clang, no shadow sampler.
- Translation: `generated-newer`, `NSMBW_GAME_ID=SMNE03`.
- Data home: `/tmp/opencode/nsmbw-newer-perf/data`.
- Base disc: `/home/tim/nsmbw-game`.
- Newer pack: `_references/Newer_W_1.30` through `paths.overlay_roots`.
- Input: `5000:TWO:300,8500:TWO:300,11500:TWO:300,14500:TWO:300`.
- The game resumes the save's current course. The save is a copy of the completed base save with
  `dMj2dGame_c::mCurrentWorld`/`mCurrentSubWorld` set to the target stage and the slot CRC32
  recomputed. This reaches any stage without navigating the file-select menus.
- Profile: `NSMBW_PROFILE=35:15`, then `profile_samples.py --elf <WiiCompiled> --scene S005
  --exclude-foreign`.

The PC runs the guest about four times faster than the Switch, so absolute guest CPU is a PC
number. The relative ranking and the function shares are the useful signals.

## Changes

### Desktop build

`runtime/src/hle/storage/riivolution.cpp` included the Switch RomFS headers and called
`romfsMountFromFile` unconditionally, so the current runtime no longer built on Linux. The RomFS
mount is now under `__SWITCH__`; desktop builds validate the Newer pack through
`paths.overlay_roots` by checking `NewerSMBW/NewerRes/DLCodeUS_1.bin`.

This restores the base and Newer desktop builds.

### EFB copy destinations

The GX copy path kept every destination in `std::map<uint32_t, uint32_t>`, and
`RememberEfbCopyDestination` did a tree lookup for every copy. On the `9-37` airship profile the
`std::map::try_emplace` node was 2.36% of on-CPU.

It is now a flat `std::vector<std::pair<uint32_t, uint32_t>>` sorted by guest address, with
`lower_bound` for lookup and a short erase run during invalidation. The map node left the top 20
of the next profile.

### GXBegin vertex descriptor publish

`ApplyAuroraVtxStateForBegin` memoized the whole begin publish, but the memo compared aurora's
live `vcdLo`/`vcdHi`/VAT registers with the last publish. The VAT row is clobbered between begins
by the game's NBT writes, so the memo missed every time: 1,092,000 calls, 0 hits and 1,092,000
full publishes in a 35 s run. 98.5% of the misses had an unchanged descriptor (same generation,
same `vcdLo`/`vcdHi`); only the VAT row differed.

`PublishAuroraVtxState` now takes a `publishDesc` flag. The begin path skips the 26-entry
`GXSetVtxDescv` when the HLE state has not moved and aurora's `vcdLo`/`vcdHi`/`nrmType` still equal
the last publish, and republishes only the VAT row. `NSMBW_GX_DESC_SKIP=0` restores the old
behavior for A/B.

`8-16`, interleaved pairs (guest CPU):

| `8-16` | Runs | Median |
|---|---:|---:|
| descriptor skip off | 5.23 / 5.33 / 5.19 ms | 5.23 ms |
| descriptor skip on | 4.74 / 4.74 / 5.25 ms | **4.74 ms** |

The exact profile agrees: `SETVCDATTR` 62 -> 28 samples, `GXSetVtxDescv` 26 -> 5, on-CPU samples
3,324 -> 2,516. `8-1` is neutral within its larger spread (median 5.58 vs 5.53 ms, runs from 3.9
to 6.6 ms). Rendering was checked on `8-1`, `8-16`, `9-5` and the vanilla file select.

### GXBegin VAT republish

With the descriptor skip in place, the VAT row was still republished in full (17 attributes) on
almost every begin. The cause is the game's `GX_VA_NBT` format write: it falls through to the NRM
slot in aurora's packed VAT, so the memo sees a different `vatA` on the next begin even though the
HLE state has not changed.

`HleGxState::nbtVatDirtyFmt` records which format rows an NBT write touched. When the begin path
sees an unchanged HLE generation, an unchanged descriptor and that bit, it republishes only
`GX_VA_NRM` instead of `POS..TEX7`. `NSMBW_GX_VAT_SPARSE=0` restores the full row for A/B.

Correctness is checked at runtime with `NSMBW_GX_VAT_VERIFY=1`: after a sparse restore the packed
`AuroraGetVtxFormatRegs` output must equal the row the last full publish left. Three heavy levels
(`8-16`, `4-6`, `5-8`), 30 s each: 0 mismatches.

Whole-run stage medians on `8-16` (all 2 s windows, order alternated):

| `8-16` | Runs | Median |
|---|---:|---:|
| full VAT row | 4.84 / 4.53 / 4.83 / 4.86 ms | 4.84 ms |
| NRM-only republish | 4.73 / 4.94 / 4.68 / 4.56 ms | 4.71 ms |

The saving is small (-0.13 ms) because the row is only 17 short `SETVATP` calls, but the rewrite is
strictly narrower and the verify mode shows it lands on the same register state. Rendering was
checked on `8-16`, `4-6`, `5-8` and the vanilla file select.

### Indirect dispatch: diagnostic address in the CpuContext

Every indirect call constructed a `ScopedTranslatedExecutionAddress`, which wrote the diagnostic
"active translated target" into thread-local storage and restored it. The value is only read by
fatal reporters (unmapped access, executable write, CPU dumps), so the scope is pure diagnostics,
but it ran on every indirect call.

It now writes `CpuContext::activeTranslatedTarget` on the context the dispatcher already holds, and
the reporters read it from there. The context grew 464 -> 472 bytes; the layout asserts in
`abi_bridge.cpp` cover the new field. The old thread-local field is gone from `GuestThreadState`.

PC A/B on `8-16` with the two binaries: dispatch samples per on-CPU row 0.053 (old) vs 0.052 (new),
i.e. neutral, as expected for x86 `%fs`-relative TLS. The change targets the Switch, where each
thread-local access compiles to an out-of-line `__aarch64_read_tp` call under `-mtp=soft`; the old
path paid three of them per indirect call.

### Native `SinFIdx` / `CosFIdx`

`math::SinFIdx` (0x80237BD0) and `CosFIdx` (0x80237C40) are the fixed-index table interpolators
behind rotations, particles and camera math. They are now host code in
`runtime/src/hle/nw4r_g3d_sincos_native.cpp`: reduce `|fidx|` modulo the SDA period, split it into
the GQR3-quantized index and residual with the same paired-single helpers the translated body
used, interpolate the table entry and (for sin only) restore the input's sign. Both are static-call
targets, so they are registered as guard-free leaf natives
(`MKW_KNOWN_LEAF_CPU_NATIVE` in `native_cpu_calls.inc`).

`NSMBW_SINFIDX_VERIFY=1` runs the translated body first and compares the raw f21 result for every
call. It found one real difference on the first run: `CosFIdx` is even and has no sign fix, while
the first implementation negated negative inputs like `SinFIdx`. After that fix, three levels
(`8-16`, `4-6`, `5-8`) run with zero mismatches. `NSMBW_SINFIDX=0` restores the translated bodies.

Profile on `8-16`: the translated pair carried 8+0 and 10+9 samples in the two off runs
(0.24-0.48% of on-CPU); with the native enabled neither appears. The saving is small (under 0.5%),
but it is bit-exact and removes a call boundary per trigonometric lookup.

### Reverted: per-context indirect-target memo

A one-entry memo in `InvokeIndirectCpu`/`InvokeIndirectJump` kept the last target and fast-table
pointer in the `CpuContext`, so a repeat target could skip the hash probe.

Hit rate on `8-16`: 1,000,000 hits to 2,800,000 misses in 25 s (26%). Exact-symbol A/B on the
dispatch pair (`InvokeIndirectCpu` + `InvokeIndirectJump`):

| Level | memo off | memo on |
|---|---:|---:|
| `8-16` | 3.34% / 5.84% (median 4.59%) | 4.34% / 4.87% (median 4.61%) |
| `4-6` | 6.03% / 3.82% (median 4.93%) | 4.29% / 5.54% (median 4.92%) |

Guest CPU was unchanged within the content noise. The fast-table probe is one cache-line read, and
the memo's extra table load and compares cancel the saved hash. Reverted; the remaining dispatch
cost is the call machinery (register guards, the scoped diagnostic store, the context check), not
the lookup.

#### The nonvolatile-FPR guard is not removable either

The translated callees restore their own nonvolatile FPRs through the guest ABI, so the per-call
`PpcNonvolatileFprGuard` looked redundant. `NSMBW_FPR_VERIFY=1` saved the caller's FPRs, ran the
callee without the guard and compared afterwards: 6.6M calls and 65 differences on each of
`8-16`, `4-6` and `5-8`. Every difference is `func_800BCEE0` changing f31 (24.0 -> 3.0/0.0), so one
guest function really does clobber a nonvolatile register.

Skipping the guard for translated callees (`NSMBW_FPR_SKIP=1`) measured neutral: dispatch samples
5.00%/5.64% with the guard vs 5.19%/5.23% without, guard samples 53/41 vs 79/33. Not worth the
exception list and the risk, so both the skip and the verify hook were removed again.

#### Direct-calling `ScnMdl::G3dProc` from the native group loop

The native group loop could call the translated `ScnMdl::G3dProc` entry directly for model
children, like generated translated code calls other translated code, instead of going through the
indirect dispatcher. Fixed-order profiled pairs looked 1.3 ms faster on both `8-16` and `4-6`.

That was a pairing artifact: the direct variant always ran second in each pair. A whole-run A/B
with alternating order (off, on, on, off, off, on) was neutral:

| Level | direct dispatch | indirect dispatch |
|---|---:|---:|
| `8-16` | 4.74 / 4.94 / 5.08 (4.94) | 4.74 / 4.95 / 4.71 (4.74) |
| `4-6` | 4.64 / 4.82 / 5.03 (4.82) | 4.25 / 4.63 / 5.03 (4.63) |

Reverted. Lesson for the harness: order must alternate within each pair, or a fixed order credits
the second run with the warm state.

### Native `ScnGroup` traversal

`runtime/src/hle/nw4r_g3d_scene_native.cpp` replaces the translated `ScnGroup::G3dProc`,
`DefG3dProcScnGroup`, `ScnRoot::G3dProc` and `ScnRoot::CalcMaterial` with host loops and native
child dispatch. The group function's share of the `8-1` exact-symbol on-CPU profile falls from
4.3% / 3.4% to 2.1% / 0.35%, and `8-1` guest CPU from 6.44 to 5.37 ms over three interleaved
pairs. `8-16` is inside the run-to-run spread. `NSMBW_G3D_SCENE_NATIVE=0` keeps the translated
bodies for A/B. The Worlds 8 and 9 re-scan with this build is in the scan section. The decoded task
semantics follow.

### Reverted: native `ScnRoot::CalcMaterial`

A native `0x802590F0` traversal replaced the translated `ScnRoot::CalcMaterial` /
`DefG3dProcScnGroup` loop with host recursion, using the `ScnGroup` type object to decide which
children were groups. It rendered correctly, but the PC profile was worse:

| `9-5` profile | old | native scene |
|---|---:|---:|
| scene-graph/material items | ~8% | ~15% |

The native version paid `CalcMaterialGroup` 6.61%, `CallG3dProc` 1.45% and the native override
dispatch 3.93%. The translated group loop and its `G3dProc` calls were about 6%. The extra type
checks and override dispatch cost more than the loop saved on x86. Reverted.

## World scans

Guest CPU is the median of the last six 2 s perf windows. All scans use the same save injection
and 40-45 s runs. Absolute guest CPU drifts by up to 30% between host sessions on the same level
(`8-1` measured 5.37 ms in the interleaved A/B and 3.74 ms in the scan below), so only numbers from
the same session compare; the per-level ranking is stable.

### World 8

| Level | Guest CPU | Passes | EFB |
|---|---:|---:|---:|
| 8-16 | 7.36 ms | 13 | 4 |
| 8-5 | 7.29 | 13 | 5 |
| 8-25 | 7.22 | 14 | 5 |
| 8-4 | 7.21 | 13 | 6 |
| 8-6 | 7.14 | 14 | 5 |
| 8-37 | 7.07 | 13 | 6 |
| 8-21 | 6.96 | 13 | 4 |
| 8-26 | 5.80 | 19 | 10 |
| 8-3 | 5.77 | 12 | 4 |
| 8-2 | 5.63 | 19 | 10 |
| 8-33 | 5.60 | 19 | 10 |
| 8-8 | 5.59 | 14 | 5 |
| 8-24 | 5.04 | 13 | 4 |
| 8-23 | 5.02 | 13 | 4 |
| 8-1 | 4.58 | 13 | 6 |
| 8-7 | 4.27 | 65 | 4 |
| 8-15 | 4.23 | 65 | 4 |
| 8-20 | 4.14 | 65 | 4 |

### World 9

| Level | Guest CPU | Passes | EFB |
|---|---:|---:|---:|
| 9-5 | 7.39 ms | 13 | 4 |
| 9-37 | 7.39 | 13 | 4 |
| 9-24 | 7.29 | 13 | 4 |
| 9-3 | 7.27 | 13 | 4 |
| 9-2 | 7.25 | 13 | 6 |
| 9-9 | 7.22 | 13 | 4 |
| 9-25 | 5.78 | 19 | 10 |
| 9-7 | 5.74 | 13 | 4 |
| 9-23 | 5.74 | 14 | 5 |
| 9-15 | 5.65 | 19 | 10 |
| 9-26 | 5.64 | 13 | 4 |
| 9-20 | 5.56 | 13 | 4 |
| 9-8 | 5.51 | 19 | 10 |
| 9-16 | 5.45 | 13 | 4 |
| 9-4 | 5.44 | 19 | 10 |
| 9-6 | 4.59 | 65 | 4 |
| 9-33 | 4.42 | 13 | 6 |
| 9-21 | 4.39 | 65 | 4 |
| 9-1 | 4.37 | 65 | 4 |

### Worlds 8 and 9 re-scan, final build

Nine main levels per world, same harness, current native-traversal build (40 s per level):

| World 8 | Guest CPU | World 9 | Guest CPU |
|---|---:|---|---:|
| 8-4 | 5.17 ms | 9-2 | 4.88 ms |
| 8-7 | 4.93 ms | 9-5 | 4.83 ms |
| 8-2 | 4.87 ms | 9-4 | 4.81 ms |
| 8-6 | 4.77 ms | 9-1 | 4.71 ms |
| 8-9 | 4.74 ms | 9-3 | 4.65 ms |
| 8-5 | 4.67 ms | 9-9 | 4.41 ms |
| 8-3 | 4.21 ms | 9-8 | 3.95 ms |
| 8-8 | 4.05 ms | 9-7 | 3.69 ms |
| 8-1 | 3.74 ms | 9-6 | 3.67 ms |

No main level now exceeds 5.17 ms, and the ordering is stable (8-4, 8-7 and 8-2 are the heaviest;
8-1 and 8-8 the lightest). The older `7+ ms` spike table cannot be subtracted from this one because
the host drift above (up to 30%) is larger than the change; the interleaved A/Bs in the traversal
section are the attribution.

### Worlds 8 and 9 after the descriptor skip

Same harness, build with the GXBegin descriptor skip:

| World 8 | Guest CPU | World 9 | Guest CPU |
|---|---:|---|---:|
| 8-3 | 5.44 ms | 9-7 | 5.02 ms |
| 8-7 | 4.99 ms | 9-5 | 4.92 ms |
| 8-6 | 4.98 ms | 9-6 | 4.79 ms |
| 8-2 | 4.88 ms | 9-3 | 4.68 ms |
| 8-9 | 4.63 ms | 9-2 | 4.63 ms |
| 8-8 | 4.29 ms | 9-1 | 4.62 ms |
| 8-5 | 4.08 ms | 9-9 | 4.62 ms |
| 8-4 | 3.69 ms | 9-4 | 4.37 ms |
| 8-1 | 3.63 ms | 9-8 | 4.21 ms |

Medians: World 8 4.63 ms (previous build 4.74), World 9 4.63 ms (previous 4.65). The per-level
spread (3.63-5.44 ms) is larger than that difference, so the interleaved `8-16` A/B and the
`SETVCDATTR`/`GXSetVtxDescv` sample drop remain the attribution; the scan confirms no regression.

### World 4 check and the current 1-7 peak

All nine World 4 main levels plus the previous peak level of every other world (1-7), same session,
40 s each:

| World 4 | Guest CPU | Other worlds | Guest CPU |
|---|---:|---|---:|
| 4-6 | 4.97 ms | 5-8 | 4.92 ms |
| 4-9 | 4.94 ms | 7-6 | 4.62 ms |
| 4-4 | 4.78 ms | 2-6 | 4.49 ms |
| 4-2 | 4.71 ms | 3-3 | 4.47 ms |
| 4-5 | 4.56 ms | 1-3 | 4.46 ms |
| 4-3 | 4.55 ms | 6-8 | 3.38 ms |
| 4-1 | 4.49 ms | | |
| 4-7 | 3.69 ms | | |
| 4-8 | 3.49 ms | | |

World 4 is now the heaviest early world (4-6 at 4.97 ms, 4-9 at 4.94), with World 5's `5-8`
level with it at 4.92 ms; the three are inside the per-level spread. The old ranking put `6-8` on
top at 5.79 ms, but it now measures 3.38 ms: it is a group-heavy level and benefits most from the
native traversal, and the host state differs from the old session.

After the VAT and dispatch changes the same heavy levels measure: `4-6` 4.33, `4-9` 3.03,
`5-8` 4.42, `8-3` 4.97 and `9-7` 4.42 ms. None is above 5.0 ms in that run. The host drift caveat
still applies, so these are not subtracted from the table above; they confirm the heavy set and
that nothing regressed.

The old non-main spike subworlds were re-measured with the current build:

| Level | Old | Current | Level | Old | Current |
|---|---:|---:|---|---:|---:|
| 9-24 | 7.29 ms | 5.12 ms | 9-2 | 7.25 ms | 4.86 ms |
| 8-25 | 7.22 ms | 5.08 ms | 8-37 | 7.07 ms | 4.56 ms |
| 9-3 | 7.27 ms | 5.08 ms | 9-9 | 7.22 ms | 4.51 ms |
| | | | 8-16 | 7.36 ms | 3.71 ms |

No measured level is above 5.5 ms now. The remaining worst cluster is `8-3`, `9-24`, `8-25`,
`9-3`, `9-7` and `9-5` at about 5.0-5.4 ms; the old `7+ ms` entries no longer reproduce, subject
to the same session drift.

The earlier Worlds 1-7 table below was taken before the traversal and the descriptor skip; its
absolute values are superseded, but its per-world ordering for the light levels still holds.

### Worlds 1-7

Main levels only, same save injection. The highest and lowest level in each world:

| World | Highest | Guest CPU | Lowest | Guest CPU |
|---|---|---:|---|---:|
| 1 | 1-3 | 5.59 ms | 1-1 | 3.87 ms |
| 2 | 2-6 | 5.21 ms | 2-9 | 4.04 ms |
| 3 | 3-3 | 5.73 ms | 3-8 | 4.15 ms |
| 4 | 4-9 | 5.64 ms | 4-8 | 4.39 ms |
| 5 | 5-8 | 5.49 ms | 5-3 | 3.40 ms |
| 6 | 6-8 | 5.79 ms | 6-3 | 4.83 ms |
| 7 | 7-6 | 5.57 ms | 7-2 | 4.43 ms |

Worlds 1-7 stay at or below 5.79 ms. The 7+ ms group is only World 8 and World 9, so the
slowdown is specific to those later levels and their denser object/material sets, not a global
regression.

## Profiles

### `8-1`, snow level

Guest CPU 4.58 ms. Top shares of on-CPU:

| Item | Share |
|---|---:|
| `dSys_c::beginFrame` | 5.86% |
| `ScnRoot::CalcMaterial` | 5.37% |
| `m3d::calcMaterial` | 4.36% |
| `InvokeIndirectCpu` | 3.96% |
| `InvokeDirectCpu` (two hot targets) | 6.62% |
| `DefG3dProcScnGroup` | 2.22% |
| `SETVCDATTR` | 2.18% |
| `GxStream::PublishAuroraVtxState` | 1.58% |
| `GX__CallDisplayList` | 1.29% |
| `GXSetVtxAttrFmtv` | 1.13% |

### `8-16`

Guest CPU 7.36 ms. Top shares:

| Item | Share |
|---|---:|
| `InvokeIndirectCpu` | 4.74% |
| `ScnRoot::CalcMaterial` | 4.21% |
| `m3d::calcMaterial` | 3.30% |
| `dSys_c::beginFrame` | 3.04% |
| `InvokeIndirectJump` | 2.03% |
| `DefG3dProcScnGroup` | 1.97% |
| `G3dProc` | 1.60% |
| `AxDspHle::SpeakerBlock` | 1.55% |
| `GX__CallDisplayList` | 1.44% |
| `SETVCDATTR` | 1.39% |
| `CalcWorld` | 1.17% |
| `PSMTXConcat` | 1.12% |
| `SETVATP` | 1.12% |
| `MarkTexObjsDirtyForRange` | 1.07% |

### `9-37`, airship

Guest CPU 7.39 ms. Top shares before the EFB map change:

| Item | Share |
|---|---:|
| `DefG3dProcScnGroup` | 6.76% |
| `ScnRoot::CalcMaterial` | 3.78% |
| `G3dProc` | 2.60% |
| `std::map::try_emplace` (EFB copies) | 2.36% |
| `InvokeIndirectCpu` | 2.03% |
| `InvokeIndirectJump` | 1.84% |
| `SETVCDATTR` | 1.65% |
| `GXSetVtxAttrFmtv` | 1.23% |
| `HleFifoWrite` | 1.23% |
| `GxStream::PublishAuroraVtxState` | 1.18% |

### `9-5`, after the EFB map change

Guest CPU 4.3-6.5 ms across the window. Top shares:

| Item | Share |
|---|---:|
| `ScnRoot::CalcMaterial` | 3.43% |
| `DefG3dProcScnGroup` | 3.40% |
| `InvokeIndirectCpu` | 2.77% |
| `GX__CallDisplayList` | 2.64% |
| `dSys_c::beginFrame` | 2.05% |
| `DistSqSegment3ToSegment3` | 2.05% |
| `InvokeIndirectJump` | 2.01% |
| `m3d::calcMaterial` | 1.75% |
| `G3dProc` | 1.49% |
| `SETVCDATTR` | 1.45% |
| `CalcWorld` | 1.19% |
| `GxStream::PublishAuroraVtxState` | 1.06% |
| `GXSetVtxAttrFmtv` | 0.92% |
| `PSMTXConcat` | 0.83% |

### Exact-symbol profile, `9-5`

`NSMBW_PROFILE=30:15` with `scripts/profile_samples.py --exclude-foreign --guest-only`, sampled
against a frozen copy of the binary (12,045 stage samples, 2,511 on-CPU guest samples, 45 s run):

| Item | Share of guest CPU |
|---|---:|
| `ScnGroup::DefG3dProcScnGroup` (`func_80257FC0`) | 10.67% |
| `ScnRoot::G3dProc` (`func_80258B20`) | 3.50% |
| `WalkDisplayList<DlIndexScanVisitor>` | 3.35% |
| `ScnRoot::CalcMaterial` (`func_802590F0`) | 3.35% |
| `InvokeIndirectCpu` | 3.27% |
| `ScnMdl::G3dProc` (`func_8025DB30`) | 2.63% |
| `ScnMdl_G3DPROC_CALC_MAT` (`func_8025D5B0`) | 2.07% |
| `GX__CallDisplayList` (HLE) | 2.03% |
| `SETVCDATTR` | 1.87% |
| `InvokeIndirectJump` | 1.71% |
| `m3d::calcMaterial` (`func_80164D50`) | 1.31% |
| `CalcWorld` (`func_80250E70`) | 1.00% |
| `GXSetVtxAttrFmtv` | 0.96% |
| `GxStream::PublishAuroraVtxState` | 0.96% |
| `PSMTXConcat` (native) | 0.88% |
| `HleFifoWrite` | 0.80% |
| `getLoopPosX` (`func_80101AA0`) | 0.68% |

The nw4r scene traversal is 22% of Newer guest CPU: group loop, root traversal, per-model
material update, and the virtual dispatch between them. The group function alone is 10.67%, and
most of that is the per-child ABI marshalling the translator emits: every child call writes about
17 guest registers plus CR/CTR/XER into the context and reads them back, then runs
`InvokeIndirectCpu`. That is why a native loop over the same children does not win by itself.

Dispatch (`InvokeIndirectCpu` + `InvokeIndirectJump`) is 5% of guest CPU. The indirect table probe
is already a single hash probe, translated callees carry an exact nonvolatile FPR mask, and
translated entries do not pay the GPR guard, so there is no redundant save/restore to remove.

### GX display-list scan cache

`NSMBW_GX_DL_CACHE_STATS=1` reports the per-call outcome of the display-list scan cache. On `9-5`
it holds 379,619 hits of 381,000 calls (99.6%), 1,381 misses and no cache resets. The misses are
one-time first-use scans (level load and newly seen shape lists), not a per-frame cost. The
`WalkDisplayList` share above is therefore cold-scan time spread over the run, and the cache is
working as intended. The counters stay behind the environment variable.

### Reverted: native `ScnGroup::DefG3dProcScnGroup`

A native version replaced the `CALC_MAT` and default-task child loops with host code and kept the
translated body for `CALC_WORLD`, `CALC_VIEW`, `GATHER`, draw and attach/detach. On `9-5` the
interleaved runs were inside the noise (4.14/4.98 ms versus a 4.2-4.7 ms baseline), and the exact
profile already shows why: the per-child dispatch dominates and the translated loop is not the
cost. Reverted, and the shards were regenerated from the unchanged translation.

### `9-37` crash is not from the performance work

The airship level can stop with `Missing indirect jump target` for `0x534D4E45` (`"SMNE"`) in
Newer's `commonPack` (`r12`), after the stage has been running for a while. It is a guest
function-pointer stomp and it is timing dependent:

- It reproduced on the unmodified baseline binary under `NSMBW_PROFILE`, and on the earlier native
  experiment without profiling, when the run was slow or long.
- The same level runs for 50 s without a crash when the frame pacing is stable.

It is not caused by the EFB map change, the native experiments or the diagnostic counters. It is a
pre-existing Newer/host timing issue and is left out of this change.

### Native scene traversal

`runtime/src/hle/nw4r_g3d_scene_native.cpp` implements `ScnGroup::G3dProc` (0x80257F80),
`DefG3dProcScnGroup` (0x80257FC0), `ScnRoot::G3dProc` (0x80258B20) and
`ScnRoot::CalcMaterial` (0x802590F0) in host code. The translated loop writes about 17 guest
registers plus CR/CTR/XER into the context at every child call and reads them back; the native
loop keeps its state in host variables and passes only r3-r6. Child groups dispatch straight into
the native entry, so a group tree pays one native call per child instead of a translated frame
plus a registry dispatch.

The task cases follow the translated bodies:

- `CALC_WORLD`: callback timing A, virtual `CalcWorldMtx` (with the scaled `param` in a guest
  stack slot), callback B, children with `self + 0x3C`, callback C.
- `CALC_MAT`: callback A, children, callback C.
- `CALC_VIEW`: callback A, `view = camera * world` through `PSMTXConcat`, callback B, children,
  callback C.
- `GATHER`, `DRAW_OPA/Xlu`, `CALC_VTX`, `UPDATEFRAME`, task 0 and the loop-only tasks; attach and
  detach use the parent field and the vtable slot exactly as the translated switch does.

Callbacks read `mpFuncObjExec` (0xD4), `mCallbackTiming` (0xD8) and `mCallbackExecOpMask` (0xDA)
and dispatch the interface slot for the task. Child groups whose vtable holds the `G3dProc`
wrapper keep the disabled-task check. `ScnRoot::G3dProc` forwards every task to the group body
after its own disabled check, except CHILD_DETACHED for the child the root tracks; its
`CalcMaterial` tail-dispatches CALC_MAT with a zero param and info. `NSMBW_G3D_SCENE_NATIVE=0`
runs the kept translated bodies for A/B.

`8-1`, interleaved pairs, guest CPU from the stage rows and the share of the exact-symbol on-CPU
samples:

| `8-1` | guest CPU | group samples | group + root + CalcMaterial |
|---|---:|---:|---:|
| translated | 4.15 / 3.96 ms | 4.31% / 3.39% | 17.47% |
| native | 4.17 / 3.97 ms | 2.11% / 0.35% | 15.47% |

Three more interleaved pairs on each level with the installed build:

| Level | translated guest CPU (median) | native guest CPU (median) |
|---|---:|---:|
| `8-1` | 5.62 / 6.68 / 6.44 (6.44) | 5.34 / 5.37 / 5.42 (5.37) |
| `8-16` | 4.56 / 5.50 / 5.09 (5.09) | 5.33 / 5.49 / 5.60 (5.49) |

`8-1` is 1.07 ms (16.6%) lower in every native run. `8-16` shows no gain and its translated
spread (4.56-5.50 ms) is wider than the native one (5.33-5.60 ms), so the PC cannot separate the
two there. Per child the native loop does strictly less work (no 17-register marshal, no second
vtable load), which is why it stays enabled; the Switch needs the hardware A/B.

The group function itself loses most of its cost and the root chain loses about two points of
on-CPU samples. `m3d::calcMaterial`, the per-model `ScnMdl` work and the GX bridge are what
remains.

### Per-model material path: measured, not added

The pre-change `9-37` profile put `ScnMdl_G3DPROC_CALC_MAT` at 2.07% and `ScnMdl::G3dProc` at
2.63%, which made a native per-model material path the next candidate. With the traversal in place
the exact-symbol profiles of `8-16` (3,041 on-CPU samples) and `9-5` (646) do not list either
function in the top 30, so both are below 0.6%.

`m3d::calcMaterial` (0x80164D50) is a 19-line forwarder to `ScnRoot::CalcMaterial`, and an
instrumented counter shows fewer than 10,000 calls in a 35 s run, so its 2.7-3.7% sample share is
host-side attribution spill rather than per-frame work. A full native `CALC_MAT` would be a large
change for less than 0.6%, so it was not added.

What remains per frame on `8-16`: indirect dispatch (`InvokeIndirectCpu` / `InvokeIndirectJump`
and the direct instantiations), the PSQ stack helpers (about 2.5%) and the camera and 2D draw
helpers. Both halves of the GX begin publish now skip or narrow when the state has not changed:
the descriptor is skipped entirely and the VAT row is republished as one NRM entry after an NBT
write. The display-list walk shows 4-9% in individual windows but the scan
cache is at 99.6% hits, and its measured total is 1-138 ms per run depending on how much of the
level is streamed first time.

## Switch PGO

Two-pass profile-guided optimisation for the Newer build, using the repo's existing
`MKW_PGO` flow.

- Recording pass: `build-switch-newer2` configured with `-DMKW_PGO=generate`, NRO 124.3 MB.
  Installed over the launcher's Newer `game.nro` (release kept on the SD as `game.nro.release`),
  `stats = "full"`, clocks 1785/768/1600. The route covered the World 8/9/4/5 heavy levels, the
  world map, menus and a few light levels. Counters dump to `sdmc:/gcov` every 60 s.
- Profile pull: `scripts/pull_pgo_profile.py --port 5001 --build-dir build-switch-newer2` installed
  109 `.gcda`, 0 unmatched: 72 shard objects, 8 unity runtime objects and 29 other runtime objects.
- Use pass: `-DMKW_PGO=use`, NRO 96.7 MB.
- Baseline: the same source built without PGO, NRO 101.0 MB.

Console A/B (same clocks and config; world map ~60 s, then `8-3` for 2-3 minutes):

| Scene | Baseline (no PGO) | PGO | Delta |
|---|---:|---:|---:|
| `0x003` world map | 12.55 ms, 50.5 fps | 11.99 ms, 46.0 fps | -0.56 ms (-4.5%) |
| `0x005` `8-3` | 25.30 ms, 29.1 fps | 21.77 ms, 40.9 fps | **-3.53 ms (-14.0%), +11.8 fps** |

The `8-3` gain is consistent across the distribution (p25 19.50 -> 12.03 ms, p75 26.88 -> 22.66 ms,
p50 27.66 -> 23.93 ms). These are short runs with different window counts (42/25 map, 26/18 stage)
and not identical level sections, so treat the map number as indicative; the stage result is far
outside that noise. The map fps moved the other way while its guest CPU fell, consistent with the
map being GPU/pacing bound in those windows.

The recording build stopped at the end of the attract demo with no Atmosphere crash report
(`crash_reports` has nothing newer than Nov 20); the only reports are ERPT `2123-0011`
(`nn.pdmSrv.SyncPlayLog`), the usual play-log error from a forwarder launch. The last window had
1.51 GB heap used with 35 MB free, so an instrumentation-driven out-of-memory kill is likely. The
baseline and PGO builds run normally.

The PGO NRO is at `build-switch-newer2/NSMBW.nro`; the release install was restored afterwards.

## Switch host sampling and the world-map stutter

The guest profile could not explain the world-map stalls, so a real PC sampler was enabled
without instrumenting the shards: `MKW_ENABLE_HOST_SAMPLER` defines `MKW_SHADOW_SAMPLER` for the
runtime only (the host mode reads the thread context; the `-finstrument-functions` hooks are only
needed by the inaccurate shadow mode). It builds in its own `build-switch-sample` directory, so
the normal build stays clean. Console config:

```
[debug]
stats = "full"
env = "NSMBW_SAMPLE=60:60;NSMBW_SAMPLE_SCENE=0;NSMBW_SAMPLE_HOST=1;NSMBW_SAMPLE_HOST_HZ=100"
```

`host-samples.tsv` is symbolized with `scripts/host_samples.py --elf build-switch-sample/WiiCompiled.elf`
(6,000 samples, 100 Hz for 60 s):

| Sample | Before fix | After fix |
|---|---:|---:|
| `svcSendSyncRequest` (all IPC) | 18.78% | 3.98% |
| of which `fsFileWrite` | **15.0% (900 samples)** | **0.12% (7)** |
| of which `fsFileRead` | 3.7% | 3.8% |
| `steady_clock::now` | 8.88% | 9.85% |
| GX vertex publish (`GXSetVtxDescv` + `GXSetVtxAttrFmtv` + `PublishAuroraVtxState`) | 10.9% | 13.2% |
| `HleFifoWrite` | 3.4% | 4.9% |

The chain was unambiguous:

```
svcSendSyncRequest <- fsFileWrite   79.9%
svcSendSyncRequest <- fsFileRead    19.8%
```

Newer calls `OSReport` while walking on the world map ("Activating world change", "Checking
usability of path"), and `HLE_LogOSReport` flushed `std::cout` after every report while stdout was
unbuffered on Switch. Each line was therefore a blocking SD write through `fs` IPC on the guest
thread, tens of milliseconds, which is the visible walking stutter.

Fix: stdout is buffered like stderr (256 KiB) and flushed together from the existing 250 ms
background thread; `OSReport` no longer flushes per message on Switch. After the change the
`fsFileWrite` share fell from 15% to 0.12% and the stutter is gone. The remaining IPC is file
reads, which is asset streaming.

Also collected in the same profile pass:

- The map's 2D layout draws still spend about 13% in the GX vertex publish path on Switch. The
  descriptor and VAT narrowing help the begin path, but the map alternates formats per draw, so
  the per-format memo misses often.
- `steady_clock::now` plus `PerfStats::GetLevel` and `clock_gettime` are about 15% with
  `stats = "full"`. Part of that is measurement overhead; the release config keeps stats off.

## GX vertex publish on the world map

The host profile left the begin vertex publish at 13.2% of guest samples (`GXSetVtxDescv` 4.92%,
`GXSetVtxAttrFmtv` 4.33%, `PublishAuroraVtxState` 3.93%). Two separate problems were behind it.

**Double publish.** `GX__Begin` published the begin memo's vertex table, then `HleTryBeginRaw`
published the raw-FIFO table right before submitting the draw, so every raw begin published twice
and the first table was always overwritten. The raw path now publishes only when it takes the
begin. On 900k begins of the attract demo: publishes 1.80M -> 0.94M, VAT entries 26.1M -> 15.7M.

**Repeat publishes.** The raw path republished the descriptor (26 entries) and the VAT row (17
entries) on every begin even when aurora already held exactly that state.
`ApplyAuroraVtxStateForRawBegin` now keeps a per-format memo of
`(vtxStateGeneration, packed vcd/vat registers)` and skips both when they still match. The
registers are read after each publish, so the deliberate NRM clobber from the `GX_VA_NBT` entry is
part of the memo key. Same demo: publishes 83.5k, VAT entries 1.24M, descriptor publishes 48k.

The begin memo also became per-format: one entry per `GXVtxFmt` with its own generation, packed
registers and applied VAT row, and a sparse `GXSetVtxAttrFmtv` that sends only changed attributes.
If aurora's packed VAT for the format differs from the memo, the whole row is republished, which
covers the raw NBT clobber, direct `GXSetVtxAttrFmt` calls and display-list replays.
`NSMBW_GX_FMT_MEMO=0` and `NSMBW_GX_RAW_MEMO=0` restore the legacy behavior.

`NSMBW_GX_FMT_VERIFY=1` compares every sparse publish against a full one. It caught the NBT
clobber: the raw publish writes `GX_VA_NBT` last, whose default row is `cnt = GX_POS_XYZ` (1), and
aurora's `SETVAT` treats it like NRM, so it sets the NRM VAT count bit. The old single-entry memo
self-corrected on the next generation change; the per-format memo could skip it. With the VAT
divergence check the 8-16 run reports 0 mismatches.

World 4 map, host sampler, 60 s at 100 Hz, same save and walk:

| Sample | Before | After |
|---|---:|---:|
| `GXSetVtxAttrFmtv` | 4.33% | 1.57% |
| `GXSetVtxDescv` | 4.92% | 1.38% |
| `PublishAuroraVtxState` | 3.93% | 1.22% |
| total | 13.18% | 4.17% |

The after numbers were taken with the debug counters still on the hot path, so the release build is
slightly better. Switch, World 4 map, `stats = "light"`, walking, median of 16 report windows:
40.8 fps, p50 23.6 ms, game 24.0 ms, guest 22.5 ms, gx 4.1 ms. The earlier 30 fps observation was
not recorded with the same build, so treat this as the current state; the sampler table is the
controlled before/after.

## 8-3 and the render worker

8-3 at stock clocks varies with the section. A stable stretch measured p50 28.3 ms with guest
CPU 25.8 ms (`gx` 9.1, `gx fifo` 5.6) and a render worker cycle of 24.9 ms (record 9.6, submit
13.6). A later section measured 56 to 62 ms with the worker record at 26 to 30 ms, so frame-time
A/Bs within 8-3 are only meaningful for the same stretch.

The stock-clock host samples show where the guest time goes: 31.7% waiting on the frame worker
(`svcWaitProcessWideKeyAtomic`), 11.9% in mutex lock/unlock/signal, then `find_patch_targets`
4.75%, `forget_content_cached_handle` 1.30%, display list scan 1.82%, and about 5% in
`stats = "full"` timing. The worker is therefore as much of the 60 fps wall as the guest.

One frame of 8-3 does 81 small EFB copies into the animated tile atlas, all 32x32 RGB5A3/RGBA8,
plus the display copy. The patch path for each tile used to scan every cached static texture
(`find_patch_targets`), erase the content cache with an `erase_if` scan, resolve the strip into a
32x32 texture, blit that texture into a second 32x32 texture, and copy the result into the atlas.

Changes:

- Memoize `find_patch_targets` by destination with a generation counter on the source cache.
- Return the target list by reference instead of allocating and copying a vector per copy.
- Index the content cache by texture handle, so `forget_content_cached_handle` is O(1).
- When the resolve target is already the patch size and RGBA8, copy it straight into the atlas
  and skip the intermediate blit.

`NSMBW_GX_PATCH_MEMO=0` and `NSMBW_GX_PATCH_DIRECT=0` restore the old paths. A deterministic 8-16
demo capture is byte-identical with the direct path on and off. The remaining worker cost is the
renderer's pass structure, not the copies themselves.

### Scratch slot virtualization

The strip log for one 8-3 frame showed 738 strip starts in three shapes: 853x76 (light textures),
43x35 and 43x34 (tile atlas). Every tile draw reuses the same small scratch rect, so the deferred
resolve had to flush before the next tile overwrote it, one render pass per tile. That is where
the 70-pass frame came from.

Each scratch strip target now holds several slots. The first draw after a copy moves down one slot
(`slotHeight` rows), shifts its viewport and scissor by the same amount, and the following copy
reads the same rows; draws between two copies are layers of one scratch image and share the slot.
Slot capacity is `8 MiB / (width * height * 8)` clamped to 2..128 slots and 4096 rows, for strips
up to 64 KiB in area. When the rows run out the strip splits as before and the slots restart.

On the PC 8-16 demo frame:

```
                        old     slots
render passes            65       8
worker cycle (ms)      12.22    4.79
worker record (ms)      1.38    1.39
worker submit (ms)      3.18    2.84
worker prep (ms)        7.14    0.08
guest CPU (ms)          3.72    3.65
```

The deterministic capture differs from the old path by 132 pixels of 1,638,000, every one by 1 LSB
in one channel: the taller strip texture evaluates the same linear-filter UVs at a different float
magnitude. Vanilla `SMNE01` is byte-identical with slots on and off.

On the console, the same heavy 8-3 stretch (2.2 to 3.0 ms of display lists, 17.5 to 18.2 k FIFO
writes) before and after:

```
                        before   slots
p50 (ms)               53.7-62.5  41.7-48.4
guest CPU (ms)         36.6-37.8  31.8-33.5
gx (ms)                10.4-10.9   7.5-7.7
passes                    70       9-10
worker cycle (ms)        51-57     35-48
worker record (ms)       26-30     16-25
worker submit (ms)       23-25     17-21
```

That is about 20% off the frame at stock clocks. One 2 s window still reported 70 passes, so a few
scratch patterns fall back to splits.

### Shader prewarm contention

The 8k GX draws merge into 230 to 290 renderer draws, so the worker cost is not the draw count.
The remaining worker time was being taken by the background shader prewarm. Each pipeline build
holds the Dawn device lock for tens of milliseconds (Tint and the driver compile), so every render
command the frame worker records waits behind it. A 20 to 30 s 8-3 run still had about 1500 of
2500 pipelines queued, so the contention never stopped.

```
                        prewarm running   gated
p50 (ms)                   41-48           35-39
worker cycle (ms)          39-45           19
worker record (ms)         19-25           6.4
worker submit (ms)         17-20           11
```

Background prewarm now pauses while scene 0x005 (stage) or 0x003 (world map) is on screen and
resumes during boot, menus and scene transitions. `NSMBW_PREWARM=off` disables it entirely. The
worker now has headroom; the frame is guest-bound at about 32 ms.

`NSMBW_GX_SCRATCH_SLOTS=0` restores the split-per-copy path. `NSMBW_LOG_STRIP=1` logs each strip
begin/end with its shape plus `slots N` and `slots used N/M`.

### PC harness caveat

The PC scripted-input harness never reaches the world map. The save's world/node selects the
course played by the title screen's attract demo, which is what the scene log reports as
`0x005 STAGE`, and the script only moves through the controller prompt and demo. Changing the
save's subworld changes the demo course, which looks like level selection. World-map numbers must
come from the console sampler. `NSMBW_CAPTURE_FRAME` and `NSMBW_CAPTURE_PATH` write the presented
frame to a BMP; use them to check what a PC run is actually showing.

## Source review

The Newer source is available at `https://github.com/Newer-Team/NewerSMBW` and was checked against
the profile.

- `Kamek/src/linegod.cpp`: `LineGod_Update` returns immediately when the event flag state has not
  changed, and `LineGod_BuildList` only runs on create. It is not a per-frame background walk.
- `Kamek/src/thundercloud.cpp`: `dThunderCloud` spawns
  `Wm_mr_electricshock_biri02_s` every frame in `executeState_Follow` and `executeState_Wait`. That
  is a real per-frame effect, but the World 8 level data has no `WM_BUBBLE` (type 678) sprites, so
  it is not the World 8 slowdown. It remains a candidate for the World 5 sky levels.
- `dMeteor` (`meteor.cpp`), `daElectricLine` (`electricLine.cpp`) and `daFireLaser`
  (`firelaser.cpp`) update their models and timers normally; none loads a file or allocates per
  frame.
- The custom actors the profiles touch do not call a translation-hostile loop. The cost is the base
  engine work their models add.

## Vanilla verification

The EFB map change is shared by both products, so the `SMNE01` desktop build was rebuilt from the
regenerated base `generated` shards and run:

- The base game boots, presents the title screen and reaches the file-select screen with the new
  container, with no missing translated function and no GX error.
- The Newer `9-5` gameplay run exercises the same `gx_copy.cpp` and EFB copy path; its capture is
  correct.
- The base `generated/build_shards` was regenerated from the full base translation after an earlier
  shard command used the wrong metadata. The base game now builds cleanly again.
- The base NAND save used for the test was restored afterwards.
- The display-list cache counters are environment-gated and the base build was rebuilt and
  re-tested with them present: title, file select, stage and game setup all reached, no missing
  function and no fatal error.

### Reverted: native `ScnMdl_G3DPROC_CALC_MAT` fast path

A native fast path checked the common static case: no `ScnObj` callback, no TexPat / TexSrt /
MatClr animation, and zero bits in the six `CALC_MAT` dirty-buffer flags. Anything else kept a
copy of the translated body as the fallback. The first version called the guest
`GetResMatNumEntries` from the fast path; that clobbered the caller context and produced a missing
indirect-jump target (`ctr = 0x534D4E45`, "SMNE"). Reading the material count natively from the
`ResMdl` (`toResMatDic` at `+0x30`) removed the crash, but the A/B was still negative:

| `9-5` | guest CPU | fast / slow calls |
|---|---:|---:|
| EFB map only | 4.9-5.5 ms | n/a |
| native fast path | 6.2-7.3 ms | 8193 / 15789 |

Only about a third of the calls took the fast path; the other two thirds paid the fast-path checks
and then the translated body. Reverted. The same experiment confirms that reading `ResMdl` state
natively is possible, but the dirty/animated materials are the majority in this level.

## Reading

The slow levels are not dominated by one Newer custom object. The top items are the generic nw4r
scene graph and material update, the indirect-dispatch path, and the GX display-list/vertex bridge.
Newer's custom objects add models, materials and scene groups, which makes those engine paths
bigger. The animation native paths (FVS32, `AnmObjChrBlend`) are not the bottleneck.

The PC profile is now at its noise floor: after the scene traversal, GX publish and trig leaves,
the largest remaining translated items are about 1-2.5% each and several of the biggest *named*
entries are host-side attribution spill. The remaining targets, in order of expected value:

1. Indirect dispatch: `InvokeIndirectCpu` / `InvokeIndirectJump` plus the direct instantiations are
   about 10% of guest CPU. The lookup memo was tried and measured neutral (26% hit rate); the cost
   is the call machinery (guards, the scoped diagnostic store, the context check), not the probe.
   A change here has to remove per-call work at the dispatch site, not add another table.
2. PSQ stack helpers: about 1-2.5% of guest CPU in the paired-single quantized path (`SinFIdx` /
   `CosFIdx` no longer use the generic stack helper, but animation and matrix code still does).
3. Per-model material work (`ScnMdl_G3DPROC_CALC_MAT`, `m3d::calcMaterial`) is below 0.6% after the
   traversal change; a native path is not worth its risk.
4. Newer-specific PGO is now measured: -14.0% guest CPU and +11.8 fps on `8-3`, -4.5% on the
   world map (short console A/B). The profile is collected and the NRO exists; re-record it after
   code changes that alter the hot paths.
