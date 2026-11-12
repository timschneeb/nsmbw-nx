#include "perf_stats.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <string>
#if defined(__linux__) && !defined(__ANDROID__)
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <sched.h>
#include <time.h>
#endif

namespace PerfStats {
namespace {

ThreadState g_threads[RoleCount];
double g_nanosPerTick = -1.0;
uint64_t g_gxFifoNanos = 0, g_gxFifoBytes = 0, g_gxFifoCalls = 0;

const char* RoleName(int role) {
    return role == RoleAudio ? "audio mixer" : "guest";
}

#if defined(MKW_PLATFORM_SWITCH)
// InfoType_ThreadTickCount does not document its unit. A short busy wait tells
// nanoseconds from 19.2 MHz system ticks: a thread cannot use more time than the
// system clock advanced while it was spinning.
void CalibrateTickUnit() {
    u64 before = 0;
    Result rc = svcGetInfo(&before, InfoType_ThreadTickCount, threadGetCurHandle(), TickCountInfo_Total);
    if (R_FAILED(rc)) {
        RT_LOGF(RT_TAG_RUNTIME, "stats: thread tick count unavailable (0x%08x)\n", rc);
        return;
    }
    const u64 wallStart = armGetSystemTick();
    const u64 target = wallStart + armGetSystemTickFreq() / 40;  // ~25 ms
    while (armGetSystemTick() < target) {
    }
    const u64 wall = armGetSystemTick() - wallStart;
    u64 after = 0;
    rc = svcGetInfo(&after, InfoType_ThreadTickCount, threadGetCurHandle(), TickCountInfo_Total);
    const u64 delta = after - before;
    if (R_FAILED(rc) || delta == 0 || wall == 0) {
        RT_LOGF(RT_TAG_RUNTIME, "stats: thread tick count did not advance (0x%08x, %llu ticks, %llu sys)\n",
                rc, static_cast<unsigned long long>(delta),
                static_cast<unsigned long long>(wall));
        return;
    }
    // The thread was spinning for the whole wall interval, so its cpu time equals
    // the wall time and the unit follows from the tick delta.
    const double wallNs = static_cast<double>(wall) * 1e9 / static_cast<double>(armGetSystemTickFreq());
    g_nanosPerTick = wallNs / static_cast<double>(delta);
    RT_LOGF(RT_TAG_RUNTIME, "stats: thread ticks %llu in %llu system ticks (%.3f ns each)\n",
            static_cast<unsigned long long>(delta), static_cast<unsigned long long>(wall),
            g_nanosPerTick);
}
#endif

void ApplyEnvList(const std::string& list) {
    for (size_t start = 0; start < list.size();) {
        size_t end = list.find(';', start);
        if (end == std::string::npos) {
            end = list.size();
        }
        const std::string item = list.substr(start, end - start);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && eq > 0) {
            const std::string name = item.substr(0, eq);
            const std::string value = item.substr(eq + 1);
            setenv(name.c_str(), value.c_str(), 1);
            RT_LOGF(RT_TAG_RUNTIME, "debug env: %s=%s\n", name.c_str(), value.c_str());
        }
        start = end + 1;
    }
}

}  // namespace

ThreadState& Thread(int role) {
    return g_threads[role];
}

void ApplyConfig() {
#if defined(MKW_PLATFORM_SWITCH)
    CalibrateTickUnit();
    // Overlap the encode with the producer's next frame. The encode is ~10 ms of CPU on the Switch
    // (2354 draws through the encoder), so running it before the frame is submitted would put it on
    // the GX FIFO's SEALED gate and slow the whole frame. The staging map no longer waits for the
    // GPU (the producer records into CPU buffers), so preparing the next frame is cheap and the
    // encode can start right after it.
    setenv("NSMBW_OVERLAP_ENCODE", "1", 0);
#endif
    const std::string stats = RuntimeConfigFile::StatsMode();
    if (!stats.empty()) {
        setenv("NSMBW_STATS", stats.c_str(), 1);
    }
    if (stats == "full") {
        // Aurora's per-draw timing still keys off the old variable.
        setenv("NSMBW_LOG_FPS", "full", 1);
    }
    ApplyEnvList(RuntimeConfigFile::DebugEnv());
    if (Enabled()) {
        RT_LOGF(RT_TAG_RUNTIME, "stats: %s\n", Full() ? "full" : "light");
    } else {
        return;
    }
    RT_LOGF(RT_TAG_RUNTIME, "affinity: %s (guest core %d, host core 1, audio core %d)\n",
            RuntimeConfigFile::Affinity() ? "on" : "off", static_cast<int>(RuntimeConfigFile::MainCore()),
            static_cast<int>(RuntimeConfigFile::AudioCore()));
}

void Pin(int role) {
#if defined(MKW_PLATFORM_SWITCH)
    if (!RuntimeConfigFile::Affinity()) {
        return;
    }
    const int core = role == RoleAudio ? RuntimeConfigFile::AudioCore() : RuntimeConfigFile::MainCore();
    if (core < 0 || core > 2) {
        return;
    }
    const Result rc = svcSetThreadCoreMask(threadGetCurHandle(), core, 1u << core);
    if (R_FAILED(rc)) {
        RT_LOGF(RT_TAG_RUNTIME, "affinity: cannot pin the %s thread to core %d (0x%08x)\n", RoleName(role),
                core, rc);
        return;
    }
    RT_LOGF(RT_TAG_RUNTIME, "affinity: %s thread pinned to core %d\n", RoleName(role), core);
#elif defined(__linux__) && !defined(__ANDROID__)
    // The PC profile mirrors the Switch cores: guest 0, host texture/GX 1, AX 2.
    if (!RuntimeConfigFile::Affinity()) {
        return;
    }
    const int core = role == RoleAudio ? RuntimeConfigFile::AudioCore() : RuntimeConfigFile::MainCore();
    if (core < 0 || core >= CPU_SETSIZE) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        RT_LOGF(RT_TAG_RUNTIME, "affinity: cannot pin the %s thread to core %d (%s)\n", RoleName(role), core,
                std::strerror(errno));
        return;
    }
    RT_LOGF(RT_TAG_RUNTIME, "affinity: %s thread pinned to core %d\n", RoleName(role), core);
#endif
}

void PinHostThreads() {
#if defined(__linux__) && !defined(__ANDROID__)
    if (!RuntimeConfigFile::Affinity()) {
        return;
    }
    static uint64_t s_lastScanMs = 0;
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const uint64_t nowMs =
        static_cast<uint64_t>(now.tv_sec) * 1000ull + static_cast<uint64_t>(now.tv_nsec) / 1000000ull;
    if (nowMs - s_lastScanMs < 2000ull) {
        return;
    }
    s_lastScanMs = nowMs;

    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr) {
        return;
    }
    const long self = static_cast<long>(syscall(SYS_gettid));
    const int audioCore = RuntimeConfigFile::AudioCore();
    while (const dirent* entry = readdir(tasks)) {
        char* end = nullptr;
        const long tid = std::strtol(entry->d_name, &end, 10);
        if (entry->d_name[0] == '.' || end == entry->d_name || *end != '\0' || tid <= 0 || tid == self) {
            continue;
        }
        char path[64] = {};
        std::snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
        char comm[32] = {};
        if (FILE* file = std::fopen(path, "r")) {
            if (std::fgets(comm, sizeof(comm), file) == nullptr) {
                comm[0] = '\0';
            }
            std::fclose(file);
        }
        if (std::strcmp(comm, "ax-mix\n") == 0) {
            continue;
        }
        const bool audio = std::strstr(comm, "Audio") != nullptr || std::strstr(comm, "audio") != nullptr ||
                           std::strstr(comm, "module-rt") != nullptr;
        const int core = audio ? audioCore : 1;
        if (core < 0 || core >= CPU_SETSIZE) {
            continue;
        }
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        sched_setaffinity(static_cast<pid_t>(tid), sizeof(set), &set);
    }
    closedir(tasks);
#endif
}

void AddGxFifo(uint64_t nanos, uint64_t bytes) {    g_gxFifoNanos += nanos;
    g_gxFifoBytes += bytes;
    ++g_gxFifoCalls;
}

void TakeGxFifo(uint64_t& nanos, uint64_t& bytes, uint64_t& calls) {
    nanos = g_gxFifoNanos;
    bytes = g_gxFifoBytes;
    calls = g_gxFifoCalls;
    g_gxFifoNanos = g_gxFifoBytes = g_gxFifoCalls = 0;
}

namespace {
GxFifoBreakdown g_gxFifoBreakdown;
uint64_t g_gxFifoSubmitNanos = 0;
uint64_t g_gxFifoSubmitCalls = 0;
}

void AddGxFifoPart(int part, uint64_t nanos) {
    if (part < 0 || part >= GxFifoPartCount) {
        return;
    }
    g_gxFifoBreakdown.nanos[part] += nanos;
    ++g_gxFifoBreakdown.calls[part];
}

namespace {
std::atomic<uint32_t> g_fastmemMmioLoads{0};
std::atomic<uint32_t> g_fastmemMmioStores{0};
}

void AddFastmemMmioLoad(uint32_t count) noexcept {
    g_fastmemMmioLoads.fetch_add(count, std::memory_order_relaxed);
}

uint32_t TakeFastmemMmioLoads() noexcept {
    return g_fastmemMmioLoads.exchange(0, std::memory_order_relaxed);
}

void AddFastmemMmioStore(uint32_t count) noexcept {
    g_fastmemMmioStores.fetch_add(count, std::memory_order_relaxed);
}

uint32_t TakeFastmemMmioStores() noexcept {
    return g_fastmemMmioStores.exchange(0, std::memory_order_relaxed);
}

void AddGxFifoSubmit(uint64_t nanos) {
    g_gxFifoSubmitNanos += nanos;
    ++g_gxFifoSubmitCalls;
}

uint64_t TakeGxFifoSubmitNanos() {
    const uint64_t value = g_gxFifoSubmitNanos;
    g_gxFifoSubmitNanos = 0;
    g_gxFifoSubmitCalls = 0;
    return value;
}

GxFifoBreakdown TakeGxFifoBreakdown() {
    GxFifoBreakdown out = g_gxFifoBreakdown;
    g_gxFifoBreakdown = GxFifoBreakdown{};
    return out;
}

void Publish(int role) {
    if (role == RoleGuest) {
        PinHostThreads();
    }
#if defined(MKW_PLATFORM_SWITCH)
    g_threads[role].core.store(svcGetCurrentProcessorNumber(), std::memory_order_relaxed);
    if (g_nanosPerTick < 0.0) {
        return;
    }
    u64 ticks = 0;
    if (R_FAILED(svcGetInfo(&ticks, InfoType_ThreadTickCount, threadGetCurHandle(), TickCountInfo_Total))) {
        return;
    }
    g_threads[role].cpuNanos.store(static_cast<uint64_t>(static_cast<double>(ticks) * g_nanosPerTick),
                                   std::memory_order_relaxed);
#elif defined(__linux__) && !defined(__ANDROID__)
    // Publish runs on the measured thread itself, so the thread cpu clock is exact.
    g_threads[role].core.store(static_cast<int32_t>(sched_getcpu()), std::memory_order_relaxed);
    struct timespec ts = {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return;
    }
    const uint64_t now =
        static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
    g_threads[role].cpuNanos.store(now, std::memory_order_relaxed);
#endif
}

}  // namespace PerfStats
