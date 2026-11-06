#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stddef.h"
#include "stdint.h"
#endif

#ifndef NDEBUG
#define AURORA_GFX_DEBUG_GROUPS
#endif

void aurora_push_debug_group(const char* label);
void aurora_pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
  uint32_t presentedFrameCount;
  uint32_t interpolatedFrameCount;
  // Render passes the last sealed frame was split into, and how many of them targeted the EFB.
  // More than one EFB pass costs a store and reload of the full EFB color and depth per split.
  uint32_t renderPassCount;
  uint32_t efbPassCount;
  // Time the frame worker spent waiting for the staging buffer's GPU work, cumulative.
  uint64_t stagingMapWaitNanos;
  // Time spent copying the recorded frame into the staging buffer, and the bytes copied.
  uint64_t stagingCopyNanos;
  uint64_t stagedBytes;
} AuroraStats;

typedef struct {
  uint64_t totalPresentCount;
  uint32_t sampleCount;
  double framesPerSecond;
  double averageFrameTimeMs;
  double p95FrameTimeMs;
  double jitterMs;
  // framesPerSecond with duplicated presentation slots scaled out, so this is the rate of frames
  // that carried new motion. Equal to framesPerSecond when every slot replayed real interpolation.
  double effectiveFramesPerSecond;
} AuroraPresentTiming;

const AuroraStats* aurora_get_stats();
void aurora_get_present_timing(AuroraPresentTiming* timing);

// NSMBW profiling: skip one draw category ("", "blend", "depth", "nodepth", "indirect", "tev>=N",
// "tex>=N", "all") and/or the GX texture copies to measure their cost in one run. Both are intended
// for diagnostics only: visuals are broken while they are active.
void aurora_set_skip_draws(const char* filter);
void aurora_set_skip_texture_copies(int enable);
int aurora_skip_texture_copies(void);
void aurora_set_no_merge(int mode);

// NSMBW: encode the frame's EFB work as one pass after the scratch strip passes instead of one
// pass per strip entry. Removes the store/reload of the EFB color and depth at every split.
void aurora_set_fuse_efb_passes(int enable);

// Interpolation health: the per-frame fields describe the last sealed frame, the counters
// accumulate since it was configured. This answers "output FPS dropped but the game held 60".
typedef struct {
  uint32_t targetFps;          // configured target, 0 when interpolation is off
  uint32_t targetSamples;      // slots the pacing controller currently aims for
  uint32_t activeSamples;      // slots latched for the latest sealed frame
  uint32_t candidates;         // perspective draws in the latest sealed frame
  uint32_t matchable;          // candidates whose identity also existed last frame
  uint32_t matches;            // draws matched to the previous frame
  uint32_t eligible;           // latest frame inserted interpolated slots
  uint32_t replaySafe;         // latest frame could replay its command stream
  uint64_t framesSealed;
  uint64_t framesLowMatch;
  uint64_t framesReplayUnsafe;
  uint64_t slotReductions;
  uint64_t lateSealDrops;
} AuroraFrameInterpolationDiagnostics;

void aurora_get_frame_interpolation_diagnostics(AuroraFrameInterpolationDiagnostics* diagnostics);

// Generates transform-interpolated perspective frames between consecutive 60 Hz logical frames.
// Supported targets are 0 (off), 120, 180 and 240. Guest simulation and VI timing are unchanged.
void aurora_set_frame_interpolation_fps(uint32_t targetFps);
// NSMBW: V-Sync on (Fifo) or off (Immediate, else Mailbox); off by default.
void aurora_set_vsync(bool enabled);
bool aurora_get_vsync();
uint32_t aurora_get_frame_interpolation_fps();

// Newly encountered GX pipelines compile on the bounded worker queue. Draws whose pipeline is not
// ready are skipped rather than stalling submission, and pick it up once compilation finishes.
void aurora_set_skip_unready_pipelines(bool enabled);
bool aurora_get_skip_unready_pipelines();
uint32_t aurora_get_queued_pipeline_count();
// True while the boot prewarm or any queued build is still outstanding.
bool aurora_boot_pipelines_pending();
// True once the boot prewarm has drained at least once on the current pipeline cache.
bool aurora_boot_prewarm_compiled();
// Blocks until every queued boot pipeline has been built. Call before starting the guest when the
// first frames must not wait on a build.
void aurora_wait_for_boot_pipelines();
// Raises the boot prewarm above the guest once its first visible frame is on screen.
void aurora_set_prewarm_boost(bool enabled);

// Controls whether display copies bypass the Wii's vertical copy filter.
void aurora_set_disable_copy_filter(bool disabled);
bool aurora_get_disable_copy_filter();

// Guest-RAM write tracking. `generation` changes whenever guest RAM covering a host range was
// written (or returns AURORA_GUEST_WRITE_UNTRACKED); `notify` reports writes aurora made itself.
#define AURORA_GUEST_WRITE_UNTRACKED UINT64_MAX
typedef uint64_t (*AuroraGuestWriteGenerationCallback)(const void* hostPtr, size_t size);
typedef void (*AuroraGuestWriteNotifyCallback)(const void* hostPtr, size_t size);
void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                  AuroraGuestWriteNotifyCallback notify);

#ifdef __cplusplus
}
#endif

#endif
