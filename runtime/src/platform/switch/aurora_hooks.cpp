// Switch thread placement hooks called from aurora. See aurora/switch_hooks.h.
#include <switch.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include "runtime_config.h"
#include "runtime_log.h"

namespace {

// Helper placement on Horizon. The guest owns one core (MainCore, pinned by PerfStats::Pin);
// every other thread keeps a mask that includes the remaining cores so the scheduler can move it,
// while `ideal` says what to prefer. Affinity off means the guest core is unknown, so the hooks
// leave aurora's defaults alone.
int GuestCore() {
    if (!RuntimeConfigFile::Affinity()) {
        return -1;
    }
    return RuntimeConfigFile::MainCore();
}

bool HelperMask(int guestCore, u64& mask) {
    if (guestCore < 0 || guestCore > 2) {
        return false;
    }
    u64 processMask = 0;
    if (R_FAILED(svcGetInfo(&processMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return false;
    }
    mask = processMask & ~(1ull << guestCore);
    return mask != 0;
}

int LowestCore(u64 mask) {
    for (int core = 0; core <= 2; ++core) {
        if ((mask & (1ull << core)) != 0) {
            return core;
        }
    }
    return -1;
}

} // namespace

// GX decode worker (aurora-main/lib/gx/fifo.cpp). It sits on the frame's sync path: the guest
// waits for it at the end of the frame, so it runs at the guest's own priority on its own core
// instead of time-slicing at 0x3B beside the audio mixer.
extern "C" void MkwConfigureGxWorkerThread() {
    u64 mask = 0;
    const int guestCore = GuestCore();
    if (!HelperMask(guestCore, mask)) {
        return;
    }
    int core = LowestCore(mask);
    if (const char* value = std::getenv("NSMBW_GX_CORE")) {
        const int parsed = std::atoi(value);
        if (parsed >= 0 && parsed <= 2) {
            core = parsed;
        }
    }
    if (core < 0) {
        return;
    }
    svcSetThreadCoreMask(threadGetCurHandle(), core, 1ull << core);
    svcSetThreadPriority(threadGetCurHandle(), 0x2C);
}

// Frame worker (aurora-main/lib/aurora.cpp). It prefers its own core but must be able to move to
// the other helper core while the GX worker (higher priority) holds that one.
// level 0: idle, 1: prewarm below the guest (loading screen not up yet), 2: boosted prewarm.
extern "C" void MkwSetPipelineWorkerPriority(int level) {
    const Handle handle = threadGetCurHandle();
    if (level >= 2) {
        static std::atomic<bool> logged{false};
        Result rc = svcSetThreadPriority(handle, 0x20);
        if (R_FAILED(rc)) {
            rc = svcSetThreadPriority(handle, 0x2C);
        }
        if (!logged.exchange(true)) {
            RT_LOGF(RT_TAG_RUNTIME, "pipeline prewarm priority 0x20 (rc 0x%08x), fallback 0x2C\n",
                    static_cast<unsigned>(rc));
        }
    } else if (level == 1) {
        svcSetThreadPriority(handle, 0x30);
    } else {
        svcSetThreadPriority(handle, 0x3C);
    }
}

extern "C" void MkwConfigureFrameWorkerThread() {
    u64 mask = 0;
    const int guestCore = GuestCore();
    if (!HelperMask(guestCore, mask)) {
        return;
    }
    int ideal = LowestCore(mask);
    if (const char* value = std::getenv("NSMBW_HOST_CORE")) {
        const int parsed = std::atoi(value);
        if (parsed < 0) {
            return;
        }
        if (parsed <= 2) {
            ideal = parsed;
        }
    }
    if (ideal >= 0) {
        svcSetThreadCoreMask(threadGetCurHandle(), ideal, mask);
    }
}
