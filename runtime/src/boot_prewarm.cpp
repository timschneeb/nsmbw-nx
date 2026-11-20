// Boot shader gate and startup-screen timing. On the Switch's first boot the boot splash runs the
// pipeline prewarm here while the guest is parked; the same state machine dismisses the overlay's
// startup screen on every platform.
#include "settings_overlay.h"

#include <aurora/gfx.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "runtime_log.h"

#if defined(__SWITCH__)
extern "C" void NsmbwBootSplashSetProgress(float fraction, const char* status);
extern "C" size_t NsmbwMallocFreeBytes();
#endif

namespace settings_overlay {
namespace {

using Clock = std::chrono::steady_clock;

uint64_t g_presentedFrame = 0;
std::atomic_bool g_strapInputAccepted{false};
std::atomic_uint64_t g_startupDismissFrame{UINT64_MAX};
constexpr uint64_t kStrapTransitionCoverFrames = 60;

std::atomic_bool g_bootShadersReady{false};
bool g_bootShaderNotice = false;
// On the run that builds the pipeline cache, wait for the prewarm queue to appear
// before accepting a drain. Otherwise the queue is still empty when the guest
// starts, the gate opens, and the shader screen never shows.
bool g_bootShaderFirstRun = false;
bool g_bootShaderSeenQueue = false;
Clock::time_point g_bootShaderWaitStart{};
constexpr uint32_t kBootShaderNoticeThreshold = 100;
// The queue must stay empty this long before the guest is released, so a momentary dip between
// prewarm and first-frame work cannot dismiss the screen early.
constexpr auto kBootShaderDrainSettle = std::chrono::milliseconds(1500);
// Safety valve only: the gate has no wall-clock limit, so a wedged worker is caught by watching
// for a queue that stops moving instead of by the total prewarm time.
constexpr auto kBootShaderStallLimit = std::chrono::minutes(2);
Clock::time_point g_bootShaderDrainStart{};
uint32_t g_bootShaderLastQueued = UINT32_MAX;
Clock::time_point g_bootShaderProgressAt{};

// True once the queue has made no progress for kBootShaderStallLimit. Both the overlay gate and
// the Switch splash tick share this tracker.
bool BootShaderStalled(uint32_t queued, Clock::time_point now) noexcept {
    if (queued == 0) {
        return false;
    }
    if (queued != g_bootShaderLastQueued) {
        g_bootShaderLastQueued = queued;
        g_bootShaderProgressAt = now;
        return false;
    }
    return g_bootShaderProgressAt != Clock::time_point{} && now - g_bootShaderProgressAt > kBootShaderStallLimit;
}

#if defined(__SWITCH__)
// The parsed WGSL/Tint programs are the largest reclaimable set. While the prewarm is still
// building variants they are dropped on a timer; afterwards only when the arena runs short.
// Steady state outside the prewarm sits near 10 MiB, so only a drop below this is pressure.
constexpr size_t kArenaFreeWatermark = 8u << 20;
constexpr size_t kArenaCritical = 4u << 20;
constexpr auto kHeapCheckInterval = std::chrono::seconds(1);
constexpr auto kHeapReleaseCooldown = std::chrono::seconds(10);
#endif

} // namespace

void MaybeReleaseShaderModulesOnPressure() noexcept {
#if defined(__SWITCH__)
    static Clock::time_point lastCheck{};
    static Clock::time_point lastRelease{};
    const auto now = Clock::now();
    if (lastCheck != Clock::time_point{} && now - lastCheck < kHeapCheckInterval) {
        return;
    }
    lastCheck = now;
    const size_t freeBytes = NsmbwMallocFreeBytes();
    const bool prewarm = aurora_boot_pipelines_pending();
    if (!prewarm && freeBytes >= kArenaFreeWatermark) {
        return;
    }
    // Under real pressure the cooldown must not starve the release.
    if (freeBytes >= kArenaCritical && lastRelease != Clock::time_point{} &&
        now - lastRelease < kHeapReleaseCooldown) {
        return;
    }
    lastRelease = now;
    aurora_release_shader_modules_light();
    RT_LOGF(RT_TAG_RUNTIME, "[mem] arena free %zu MiB%s, released shader parse cache (%zu MiB now)\n",
            freeBytes >> 20, prewarm ? " during prewarm" : "", NsmbwMallocFreeBytes() >> 20);
#endif
}

bool BootShadersPending() noexcept { return !g_bootShadersReady.load(std::memory_order_acquire); }

bool BootShaderNoticeActive() noexcept {
    return g_bootShaderNotice && BootShadersPending();
}

void ResetBootShaderState() noexcept {
    g_strapInputAccepted.store(false, std::memory_order_relaxed);
    g_startupDismissFrame.store(UINT64_MAX, std::memory_order_relaxed);
    g_bootShadersReady.store(false, std::memory_order_relaxed);
    g_bootShaderNotice = false;
    g_bootShaderWaitStart = {};
    g_bootShaderDrainStart = {};
    g_bootShaderLastQueued = UINT32_MAX;
    g_bootShaderProgressAt = {};
    // The boot screen is only for the first run that builds the pipeline cache. Once the prewarm
    // has drained on this cache, boot straight into the game and compile anything new in the
    // background.
    g_bootShaderFirstRun = !aurora_boot_prewarm_compiled();
    if (aurora_dawn_cache_was_reset()) {
        // The blob cache was deleted, so every prewarm recipe must be parsed from WGSL again.
        // Run the full blocking warmup rather than letting it grow in the background.
        g_bootShaderFirstRun = true;
    }
    g_bootShaderSeenQueue = false;
    if (!g_bootShaderFirstRun) {
        g_bootShadersReady.store(true, std::memory_order_release);
        g_strapInputAccepted.store(true, std::memory_order_relaxed);
        g_startupDismissFrame.store(0, std::memory_order_relaxed);
    }
}

void UpdateBootShaderState() noexcept {
    MaybeReleaseShaderModulesOnPressure();
    if (g_bootShadersReady.load(std::memory_order_relaxed)) {
        return;
    }
    const auto now = Clock::now();
    if (g_bootShaderWaitStart == Clock::time_point{}) {
        g_bootShaderWaitStart = now;
    }
    const uint32_t queued = aurora_get_queued_pipeline_count();
    if (queued > 0) {
        g_bootShaderSeenQueue = true;
    }
    g_bootShaderNotice |= queued > kBootShaderNoticeThreshold;
    const bool waiting_for_first_queue =
        g_bootShaderFirstRun && !g_bootShaderSeenQueue && now - g_bootShaderWaitStart < std::chrono::seconds(10);
    if (aurora_boot_pipelines_pending() || waiting_for_first_queue) {
        g_bootShaderDrainStart = {};
    } else if (g_bootShaderDrainStart == Clock::time_point{}) {
        g_bootShaderDrainStart = now;
    }
    const bool drained = g_bootShaderDrainStart != Clock::time_point{} &&
                         now - g_bootShaderDrainStart > kBootShaderDrainSettle;
    const bool stalled = BootShaderStalled(queued, now);
    if (stalled) {
        RT_LOGF(RT_TAG_RUNTIME, "[boot] shader prewarm stalled with %u pipelines queued; starting the game\n",
                queued);
        MarkBootShadersReady();
    } else if (drained) {
        g_bootShadersReady.store(true, std::memory_order_release);
    }
    if (BootShadersPending()) {
        // The startup screen is on screen now. Start the prewarm and let it have the console; the
        // guest must not be the trigger, because it cannot present while this screen is up.
        aurora_set_prewarm_boost(true);
    }
}

void TickBootPrewarmOnSplash() noexcept {
#if defined(__SWITCH__)
    if (!BootShadersPending()) {
        return;
    }
    static Clock::time_point lastDraw{};
    static Clock::time_point doneAt{};
    static uint32_t total = 0;
    static bool started = false;

    const Clock::time_point now = Clock::now();
    if (!started) {
        started = true;
        total = aurora_get_queued_pipeline_count();
        // Background priority: the boot splash redraws on the guest thread while the prewarm
        // runs, and a boosted worker would starve those updates.
        aurora_prewarm_allow();
    }

    if (doneAt == Clock::time_point{}) {
        if (lastDraw == Clock::time_point{} || now - lastDraw >= std::chrono::milliseconds(200)) {
            lastDraw = now;
            const uint32_t remaining = aurora_get_queued_pipeline_count();
            const float done = total > 0
                                   ? 1.0f - static_cast<float>(remaining) / static_cast<float>(total)
                                   : 1.0f;
            char status[96];
            if (aurora_dawn_cache_was_reset()) {
                std::snprintf(status, sizeof(status),
                              "Shader database was corrupted; rebuilding: %u left", remaining);
            } else {
                std::snprintf(status, sizeof(status), "Compiling shaders: %u remaining", remaining);
            }
            NsmbwBootSplashSetProgress(done, status);
        }
        // The parsed Tint programs dominate the live set. Releasing them as the prewarm runs keeps
        // the arena near the retained pipeline size instead of growing to the whole heap.
        MaybeReleaseShaderModulesOnPressure();
        const uint32_t remaining = aurora_get_queued_pipeline_count();
        const bool drained = !aurora_boot_pipelines_pending();
        const bool stalled = BootShaderStalled(remaining, now);
        if (stalled) {
            RT_LOGF(RT_TAG_RUNTIME, "[boot] shader prewarm stalled with %u pipelines queued\n", remaining);
        }
        if (drained || stalled) {
            // The compiled pipelines stay in g_pipelines. Releasing the shader module cache drops
            // the last external refs, so Dawn frees the parsed Tint programs instead of holding
            // them for the whole session.
            static bool releasedShaderModules = false;
            if (!releasedShaderModules) {
                releasedShaderModules = true;
                const size_t before = NsmbwMallocFreeBytes();
                aurora_release_shader_modules();
                RT_LOGF(RT_TAG_RUNTIME, "[mem] released shader parse cache: free %zu -> %zu MiB\n",
                        before >> 20, NsmbwMallocFreeBytes() >> 20);
            }
            doneAt = now;
            NsmbwBootSplashSetProgress(1.0f, stalled ? "Shader prewarm stalled, starting game..."
                                                     : "Shaders ready, starting game...");
        }
        return;
    }

    if (now - doneAt > std::chrono::milliseconds(500)) {
        MarkBootShadersReady();
    }
#endif
}

void MarkBootShadersReady() noexcept {
    g_bootShadersReady.store(true, std::memory_order_release);
    g_bootShaderNotice = false;
    g_strapInputAccepted.store(true, std::memory_order_relaxed);
    g_startupDismissFrame.store(0, std::memory_order_relaxed);
}

bool StartupScreenVisible() noexcept {
    return !g_strapInputAccepted.load(std::memory_order_acquire) ||
           !g_bootShadersReady.load(std::memory_order_acquire) ||
           g_presentedFrame < g_startupDismissFrame.load(std::memory_order_relaxed);
}

void NotifyStrapInputAccepted() noexcept {
    bool expected = false;
    if (g_strapInputAccepted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        g_startupDismissFrame.store(g_presentedFrame + kStrapTransitionCoverFrames,
                                    std::memory_order_release);
    }
}

void AdvancePresentedFrame() noexcept { ++g_presentedFrame; }

} // namespace settings_overlay
