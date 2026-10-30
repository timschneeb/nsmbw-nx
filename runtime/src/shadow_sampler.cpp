// Switch shadow sampler. Translated functions update a shadow PC on entry and exit; a
// sampler thread pinned off the guest core reads it at a fixed rate. The instrumentation
// only runs while the target scene is being sampled and only on the guest host thread.
#include "shadow_sampler.h"

#include "abi_bridge.h"
#include "fiber_manager.h"
#include "memory.h"
#include "runtime_log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SHADOW_NOINSTRUMENT __attribute__((no_instrument_function))
#else
#define SHADOW_NOINSTRUMENT
#endif

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_sampling{false};
std::atomic<bool> g_stop{false};
std::atomic<uintptr_t> g_shadowPc{0};
std::atomic<uint64_t> g_shadowTick{0};
std::atomic<uint32_t> g_scene{0xFFFFu};
std::atomic<uint32_t> g_frame{0};
std::atomic<uint32_t> g_targetScene{0x005u};

std::filesystem::path g_logDirectory;
std::thread g_samplerThread;
double g_startSeconds = 0.0;
double g_lengthSeconds = 0.0;
uint32_t g_sampleHz = 1000;
uint32_t g_sampleIntervalUs = 1000;
uint64_t g_staleTicks = 0;
uint64_t g_tickFrequency = 1;

uint64_t EnvU64(const char* name, uint64_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const uint64_t parsed = std::strtoull(value, &end, 0);
    return end == value ? fallback : parsed;
}

double EnvDouble(const char* name, double fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    return end == value ? fallback : parsed;
}

uint64_t NowTick() {
#if defined(MKW_PLATFORM_SWITCH)
    return armGetSystemTick();
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     Clock::now().time_since_epoch())
                                     .count());
#endif
}

uint64_t TickFrequency() {
#if defined(MKW_PLATFORM_SWITCH)
    return armGetSystemTickFreq();
#else
    return 1000000000ull;
#endif
}

struct Sample {
    uint64_t tick = 0;
    uintptr_t host = 0;
    uint32_t guest = 0;
    uint32_t scene = 0;
    uint32_t frame = 0;
};

#if defined(MKW_PLATFORM_SWITCH)
// NSMBW_SAMPLE_HOST: sample the guest thread's host PC by pausing it briefly.
// The handle is captured on the guest thread at init.
std::atomic<Handle> g_guestThread{0};

constexpr size_t kHostFrameCount = 6;

struct HostSample {
    uint64_t tick = 0;
    std::array<uintptr_t, kHostFrameCount> frames{};
};
#endif

void SleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void WriteReport(const std::vector<Sample>& samples, uint64_t startTick, uint64_t endTick) {
    if (samples.empty()) {
        RT_LOGF(RT_TAG_RUNTIME, "sample: no guest samples collected\n");
        return;
    }

    std::unordered_map<uint32_t, uint32_t> byGuest;
    for (const Sample& sample : samples) {
        if (sample.guest != 0) {
            byGuest[sample.guest]++;
        }
    }

    const std::filesystem::path path = g_logDirectory / "shadow-samples.tsv";
    if (FILE* file = std::fopen(path.string().c_str(), "w")) {
        // The guest address comes from the registry's host index, which only knows entry points.
        // Every sample also carries the raw host PC so it can be re-symbolized against the ELF.
        const uintptr_t ref = reinterpret_cast<uintptr_t>(&NsmbwShadowSamplerInit);
        std::fprintf(file, "# ref %llX module %s\n", static_cast<unsigned long long>(ref),
                     "WiiCompiled");
        std::fprintf(file, "scene\tframe\ttick_us\tguest\thost\tfunction\n");
        for (const Sample& sample : samples) {
            const TranslatedFunctionInfo* info = TranslatedFunctionRegistry::FindByAddressPtr(sample.guest);
            const char* name = info != nullptr ? info->name : "?";
            const uint64_t micros = (sample.tick - startTick) * 1000000ull / g_tickFrequency;
            std::fprintf(file, "S%03X\t%u\t%llu\t%08X\t%llX\t%s\n", sample.scene, sample.frame,
                         static_cast<unsigned long long>(micros), sample.guest,
                         static_cast<unsigned long long>(sample.host), name);
        }
        std::fclose(file);
        RT_LOGF(RT_TAG_RUNTIME, "sample: %zu samples written to %s\n", samples.size(),
                path.string().c_str());
    }

    std::vector<std::pair<uint32_t, uint32_t>> functions(byGuest.begin(), byGuest.end());
    std::sort(functions.begin(), functions.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    const double duration = static_cast<double>(endTick - startTick) / static_cast<double>(g_tickFrequency);
    RT_LOGF(RT_TAG_RUNTIME, "sample: %zu samples over %.2f s\n", samples.size(), duration);
    for (size_t i = 0; i < functions.size() && i < 40; ++i) {
        const TranslatedFunctionInfo* info = TranslatedFunctionRegistry::FindByAddressPtr(functions[i].first);
        const char* name = info != nullptr ? info->name : "?";
        RT_LOGF(RT_TAG_RUNTIME, "sample: %5.1f%%  %08X %s\n",
                100.0 * functions[i].second / samples.size(), functions[i].first, name);
    }
}


#if defined(MKW_PLATFORM_SWITCH)
void WriteHostReport(const std::vector<HostSample>& samples, uint64_t startTick, uint64_t endTick) {
    const std::filesystem::path path = g_logDirectory / "host-samples.tsv";
    const uintptr_t ref = reinterpret_cast<uintptr_t>(&NsmbwShadowSamplerInit);
    if (FILE* file = std::fopen(path.string().c_str(), "w")) {
        std::fprintf(file, "# ref %llX module WiiCompiled\n", static_cast<unsigned long long>(ref));
        std::fprintf(file, "tick_us\tpc\tlr\tfp0\tfp1\tfp2\tfp3\n");
        for (const HostSample& sample : samples) {
            const uint64_t micros = (sample.tick - startTick) * 1000000ull / g_tickFrequency;
            std::fprintf(file, "%llu", static_cast<unsigned long long>(micros));
            for (uintptr_t frame : sample.frames) {
                std::fprintf(file, "\t%llX", static_cast<unsigned long long>(frame));
            }
            std::fputc('\n', file);
        }
        std::fclose(file);
        RT_LOGF(RT_TAG_RUNTIME, "sample: %zu host samples over %.2f s written to %s\n", samples.size(),
                static_cast<double>(endTick - startTick) / static_cast<double>(g_tickFrequency),
                path.string().c_str());
    }
}

void RunHostSampler(uint32_t targetScene, uint64_t startTick, uint64_t endTick) {
    const Handle thread = g_guestThread.load(std::memory_order_acquire);
    if (thread == 0) {
        RT_LOGF(RT_TAG_RUNTIME, "sample: host mode has no guest thread handle\n");
        return;
    }
    const uint32_t hz = std::max<uint32_t>(
        1u, std::min<uint32_t>(static_cast<uint32_t>(EnvU64("NSMBW_SAMPLE_HOST_HZ", 20)), 1000u));
    const uint32_t intervalUs = std::max<uint32_t>(1u, 1000000u / hz);
    const size_t capacity = static_cast<size_t>(g_lengthSeconds * (hz + 500.0)) + 1000;
    std::vector<HostSample> samples;
    samples.reserve(capacity);
    RT_LOGF(RT_TAG_RUNTIME, "sample: host recording scene 0x%03X for %.1f s at %u Hz\n", targetScene,
            g_lengthSeconds, hz);
    auto nextSample = Clock::now();
    while (!g_stop.load(std::memory_order_relaxed) && NowTick() < endTick) {
        nextSample += std::chrono::microseconds(intervalUs);
        std::this_thread::sleep_until(nextSample);
        if (targetScene != 0 && g_scene.load(std::memory_order_relaxed) != targetScene) {
            break;
        }
        if (R_FAILED(svcSetThreadActivity(thread, ThreadActivity_Paused))) {
            SleepMs(1);
            continue;
        }
        ThreadContext context{};
        const Result rc = svcGetThreadContext3(&context, thread);
        std::array<uintptr_t, kHostFrameCount> frames{};
        frames[0] = static_cast<uintptr_t>(context.pc.x);
        frames[1] = static_cast<uintptr_t>(context.lr);
        uint64_t fp = context.fp;
        for (size_t i = 2; i < frames.size(); ++i) {
            if (fp == 0 || fp > 0xFFFFFFFFull || (fp & 7u) != 0) {
                break;
            }
            uint32_t nextLo = 0, nextHi = 0, retLo = 0, retHi = 0;
            if (!Memory::TryRead32(static_cast<uint32_t>(fp), nextLo) ||
                !Memory::TryRead32(static_cast<uint32_t>(fp) + 4u, nextHi) ||
                !Memory::TryRead32(static_cast<uint32_t>(fp) + 8u, retLo) ||
                !Memory::TryRead32(static_cast<uint32_t>(fp) + 12u, retHi)) {
                break;
            }
            const uint64_t next = (static_cast<uint64_t>(nextHi) << 32) | nextLo;
            const uint64_t ret = (static_cast<uint64_t>(retHi) << 32) | retLo;
            if (ret == 0) {
                break;
            }
            frames[i] = static_cast<uintptr_t>(ret);
            if (next <= fp || next - fp > (1ull << 20)) {
                break;
            }
            fp = next;
        }
        svcSetThreadActivity(thread, ThreadActivity_Runnable);
        if (R_FAILED(rc)) {
            continue;
        }
        samples.push_back(HostSample{NowTick(), frames});
        if (samples.size() >= capacity) {
            break;
        }
        if (Clock::now() > nextSample + std::chrono::milliseconds(10)) {
            nextSample = Clock::now();
        }
    }
    WriteHostReport(samples, startTick, NowTick());
}
#endif

void Shutdown() {
    g_stop.store(true, std::memory_order_release);
    g_sampling.store(false, std::memory_order_release);
    if (g_samplerThread.joinable()) {
        g_samplerThread.join();
    }
}

void SamplerThread() {
#if defined(MKW_PLATFORM_SWITCH)
    const int core = static_cast<int>(EnvU64("NSMBW_SAMPLE_CORE", 1));
    if (core >= 1 && core <= 2) {
        svcSetThreadCoreMask(threadGetCurHandle(), core, 1ull << core);
    }
    svcSetThreadPriority(threadGetCurHandle(), 0x2C);
    RT_LOGF(RT_TAG_RUNTIME, "sample: sampler pinned to core %d\n", core);
#endif

    const uint32_t targetScene = g_targetScene.load(std::memory_order_relaxed);
    if (targetScene != 0) {
        while (!g_stop.load(std::memory_order_relaxed) &&
               g_scene.load(std::memory_order_relaxed) != targetScene) {
            SleepMs(5);
        }
    }
    if (g_stop.load(std::memory_order_relaxed)) {
        return;
    }

    if (g_startSeconds > 0.0) {
        const auto deadline = Clock::now() + std::chrono::duration<double>(g_startSeconds);
        while (!g_stop.load(std::memory_order_relaxed) && Clock::now() < deadline) {
            SleepMs(5);
        }
    }
    if (g_stop.load(std::memory_order_relaxed)) {
        return;
    }

    const uint64_t startTick = NowTick();
    const uint64_t endTick = startTick + static_cast<uint64_t>(g_lengthSeconds * g_tickFrequency);
#if defined(MKW_PLATFORM_SWITCH)
    if (EnvU64("NSMBW_SAMPLE_HOST", 0) != 0) {
        RunHostSampler(targetScene, startTick, endTick);
        return;
    }
#endif
    const size_t capacity = static_cast<size_t>(g_lengthSeconds * (g_sampleHz + 500.0)) + 1000;
    std::vector<Sample> samples;
    samples.reserve(capacity);

    g_sampling.store(true, std::memory_order_release);
    RT_LOGF(RT_TAG_RUNTIME, "sample: recording scene 0x%03X for %.1f s at %u Hz\n", targetScene,
            g_lengthSeconds, g_sampleHz);

    auto nextSample = Clock::now();
    while (!g_stop.load(std::memory_order_relaxed) && NowTick() < endTick) {
        nextSample += std::chrono::microseconds(g_sampleIntervalUs);
        std::this_thread::sleep_until(nextSample);
        const uint32_t scene = g_scene.load(std::memory_order_relaxed);
        if (targetScene != 0 && scene != targetScene) {
            break;
        }
        const uint64_t tick = g_shadowTick.load(std::memory_order_acquire);
        const uint64_t now = NowTick();
        if (tick == 0 || now - tick > g_staleTicks) {
            continue;
        }
        const uintptr_t pc = g_shadowPc.load(std::memory_order_acquire);
        if (pc == 0) {
            continue;
        }
        const auto info = TranslatedFunctionRegistry::FindByHostAddress(pc);
        Sample sample;
        sample.tick = now;
        sample.host = pc;
        sample.guest = info.has_value() ? info->address : 0;
        sample.scene = scene;
        sample.frame = g_frame.load(std::memory_order_relaxed);
        samples.push_back(sample);
        if (samples.size() >= capacity) {
            break;
        }
        if (Clock::now() > nextSample + std::chrono::milliseconds(10)) {
            nextSample = Clock::now();
        }
    }

    g_sampling.store(false, std::memory_order_release);
    WriteReport(samples, startTick, NowTick());
}

}  // namespace

#if defined(MKW_SHADOW_SAMPLER)
extern "C" SHADOW_NOINSTRUMENT void __cyg_profile_func_enter(void* this_fn, void*) {
    if (!g_sampling.load(std::memory_order_relaxed) || !Fiber::t_isGuestHostThread) {
        return;
    }
    g_shadowPc.store(reinterpret_cast<uintptr_t>(this_fn), std::memory_order_relaxed);
    g_shadowTick.store(NowTick(), std::memory_order_relaxed);
}

extern "C" SHADOW_NOINSTRUMENT void __cyg_profile_func_exit(void*, void* call_site) {
    if (!g_sampling.load(std::memory_order_relaxed) || !Fiber::t_isGuestHostThread) {
        return;
    }
    g_shadowPc.store(reinterpret_cast<uintptr_t>(call_site), std::memory_order_relaxed);
    g_shadowTick.store(NowTick(), std::memory_order_relaxed);
}
#endif

void NsmbwShadowSamplerInit(const std::filesystem::path& logDirectory) {
#if defined(MKW_SHADOW_SAMPLER)
    if (g_enabled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
#if defined(MKW_PLATFORM_SWITCH)
    g_guestThread.store(threadGetCurHandle(), std::memory_order_release);
#endif
    const char* value = std::getenv("NSMBW_SAMPLE");
    if (value == nullptr || value[0] == '\0' ||
        std::sscanf(value, "%lf:%lf", &g_startSeconds, &g_lengthSeconds) != 2 ||
        g_lengthSeconds <= 0.0) {
        g_enabled.store(false, std::memory_order_release);
        return;
    }
    g_logDirectory = logDirectory;
    g_tickFrequency = TickFrequency();
    g_targetScene.store(static_cast<uint32_t>(EnvU64("NSMBW_SAMPLE_SCENE", 0x005)),
                        std::memory_order_relaxed);
    g_sampleHz = static_cast<uint32_t>(EnvU64("NSMBW_SAMPLE_HZ", 1000));
    g_sampleHz = std::max<uint32_t>(100, std::min<uint32_t>(8000, g_sampleHz));
    g_sampleIntervalUs = std::max<uint32_t>(1, 1000000u / g_sampleHz);
    g_staleTicks = static_cast<uint64_t>(
        EnvDouble("NSMBW_SAMPLE_STALE_MS", 3.0) * static_cast<double>(g_tickFrequency) / 1000.0);
    g_stop.store(false, std::memory_order_relaxed);
    g_sampling.store(false, std::memory_order_relaxed);
    std::atexit(Shutdown);
    try {
        g_samplerThread = std::thread(SamplerThread);
    } catch (const std::system_error& error) {
        g_enabled.store(false, std::memory_order_release);
        RT_LOGF(RT_TAG_RUNTIME, "sample: cannot start sampler: %s\n", error.what());
        return;
    }
    RT_LOGF(RT_TAG_RUNTIME, "sample: enabled, scene 0x%03X, start %.1f s, length %.1f s, %u Hz\n",
            g_targetScene.load(std::memory_order_relaxed), g_startSeconds, g_lengthSeconds, g_sampleHz);
#else
    (void)logDirectory;
#endif
}

void NsmbwShadowSamplerSetScene(uint32_t scene) {
    g_scene.store(scene, std::memory_order_relaxed);
}

void NsmbwShadowSamplerFrame(uint64_t frame) {
    g_frame.store(static_cast<uint32_t>(frame), std::memory_order_relaxed);
}
