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
Clock::time_point g_bootShaderWaitStart{};
constexpr uint32_t kBootShaderNoticeThreshold = 100;
// The queue must stay empty this long before the guest is released, so a momentary dip between
// prewarm and first-frame work cannot dismiss the screen early.
constexpr auto kBootShaderDrainSettle = std::chrono::milliseconds(1500);
// Safety valve only: the guest is gated on the pipelines, so this guards a wedged worker.
constexpr auto kBootShaderWaitLimit = std::chrono::minutes(10);
Clock::time_point g_bootShaderDrainStart{};

} // namespace

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
    // The boot screen is only for the first run that builds the pipeline cache. Once the prewarm
    // has drained on this cache, boot straight into the game and compile anything new in the
    // background.
    if (aurora_boot_prewarm_compiled()) {
        g_bootShadersReady.store(true, std::memory_order_release);
        g_strapInputAccepted.store(true, std::memory_order_relaxed);
        g_startupDismissFrame.store(0, std::memory_order_relaxed);
    }
}

void UpdateBootShaderState() noexcept {
    if (g_bootShadersReady.load(std::memory_order_relaxed)) {
        return;
    }
    const auto now = Clock::now();
    if (g_bootShaderWaitStart == Clock::time_point{}) {
        g_bootShaderWaitStart = now;
    }
    const uint32_t queued = aurora_get_queued_pipeline_count();
    g_bootShaderNotice |= queued > kBootShaderNoticeThreshold;
    if (aurora_boot_pipelines_pending()) {
        g_bootShaderDrainStart = {};
    } else if (g_bootShaderDrainStart == Clock::time_point{}) {
        g_bootShaderDrainStart = now;
    }
    const bool drained = g_bootShaderDrainStart != Clock::time_point{} &&
                         now - g_bootShaderDrainStart > kBootShaderDrainSettle;
    if (drained || now - g_bootShaderWaitStart > kBootShaderWaitLimit) {
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
    static Clock::time_point start{};
    static Clock::time_point lastDraw{};
    static Clock::time_point doneAt{};
    static uint32_t total = 0;
    static bool started = false;

    const Clock::time_point now = Clock::now();
    if (!started) {
        started = true;
        total = aurora_get_queued_pipeline_count();
        start = now;
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
            char status[64];
            std::snprintf(status, sizeof(status), "Compiling shaders: %u remaining", remaining);
            NsmbwBootSplashSetProgress(done, status);
        }
        // The parsed Tint programs dominate the live set. Releasing them as the prewarm runs keeps
        // the arena near the retained pipeline size instead of growing to the whole heap.
        static Clock::time_point lastShaderRelease{};
        if (lastShaderRelease == Clock::time_point{} ||
            now - lastShaderRelease >= std::chrono::seconds(10)) {
            lastShaderRelease = now;
            aurora_release_shader_modules_light();
        }
        if (!aurora_boot_pipelines_pending() || now - start > std::chrono::minutes(10)) {
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
            NsmbwBootSplashSetProgress(1.0f, "Shaders ready, starting game...");
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
