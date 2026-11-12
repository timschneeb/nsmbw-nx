#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Statistics levels. Light is cheap enough to leave on for every A/B run: one
// syscall per frame and a two second log line. Full adds idle attribution and
// per-draw timings, which are for attribution runs only.
namespace PerfStats {

enum class Level : int { Off = 0, Light = 1, Full = 2 };
enum Role : int { RoleGuest = 0, RoleAudio = 1, RoleCount = 2 };

inline Level ReadLevel() {
    const char* value = std::getenv("NSMBW_STATS");
    if (value == nullptr || value[0] == '\0') {
        value = std::getenv("NSMBW_LOG_FPS");
    }
    if (value == nullptr || value[0] == '0' || std::strcmp(value, "off") == 0) {
        return Level::Off;
    }
    return std::strcmp(value, "full") == 0 ? Level::Full : Level::Light;
}

// Cached on first use, so ApplyConfig must run before anything reads it.
inline Level GetLevel() {
    static const Level level = ReadLevel();
    return level;
}

inline bool Enabled() {
    return GetLevel() != Level::Off;
}

inline bool Full() {
    return GetLevel() == Level::Full;
}

struct ThreadState {
    std::atomic<uint64_t> cpuNanos{0};
    std::atomic<int32_t> core{-1};
};

ThreadState& Thread(int role);
// Publishes this thread's cpu time and current core for the stats line.
void Publish(int role);
// Applies [debug] env, stats mode and calibration. Call once from main.
void ApplyConfig();
// Pins the calling thread when [debug] affinity is on.
void Pin(int role);
// Linux PC: keeps every non-guest, non-mixer host thread on the host core, so
// the guest core matches the Switch split. Throttled internally.
void PinHostThreads();

// Guest-thread GX FIFO translation cost, accumulated by gx_fifo.cpp and read
// from the same thread by the stats line.
void AddGxFifo(uint64_t nanos, uint64_t bytes);
void TakeGxFifo(uint64_t& nanos, uint64_t& bytes, uint64_t& calls);

// Which part of the FIFO translation the time went to, so one run names the
// dominant handler instead of the whole path.
enum GxFifoPart : int { GxFifoBp = 0, GxFifoDisplayList = 1, GxFifoDraw = 2, GxFifoPartCount = 3 };
struct GxFifoBreakdown {
    uint64_t nanos[GxFifoPartCount]{};
    uint64_t calls[GxFifoPartCount]{};
};
void AddGxFifoPart(int part, uint64_t nanos);
// Time spent inside aurora::gx::GXCallDisplayList on the CALL_DL path, split out
// from the surrounding display list scan.
void AddGxFifoSubmit(uint64_t nanos);
uint64_t TakeGxFifoSubmitNanos();
GxFifoBreakdown TakeGxFifoBreakdown();

// Guest MMIO accesses the Switch fastmem handler emulated since the last take, and the time the
// handler spent on them (the exception entry and restore are outside this). Zero on PC, where
// those accesses take the inline policy test instead of faulting.
struct FastmemMmio {
    uint32_t loads = 0;
    uint32_t stores = 0;
    uint64_t nanos = 0;
};
void AddFastmemMmioLoad(uint32_t count = 1) noexcept;
void AddFastmemMmioStore(uint32_t count = 1) noexcept;
void AddFastmemMmioNanos(uint64_t nanos) noexcept;
FastmemMmio TakeFastmemMmio() noexcept;

}  // namespace PerfStats
