#if defined(NSMBW_MEMTRACE)

#include "mem_tracker.h"

#include "runtime_log.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <unwind.h>

namespace MemTracker {
std::atomic<int32_t> live_count[kBuckets] = {};
std::atomic<int64_t> live_bytes[kBuckets] = {};
} // namespace MemTracker

namespace {

std::atomic<bool> g_ready{false};

// The allocation hooks run on every thread and must not touch the SD card: a synchronous write
// there deadlocks the game while the pipeline prewarm is running. Records go to this fixed BSS ring
// and are written to the card once, from Close().
constexpr size_t kRingSize = 64ull << 20;
uint8_t g_ring[kRingSize];
std::atomic<uint32_t> g_ringPos{0};
std::atomic<uint32_t> g_dropped{0};
char g_path[256] = "sdmc:/switch/nsmbw/memtrace.bin";
uint64_t g_trace_anchor = 0;

constexpr size_t kMaxFrames = 12;
constexpr uint32_t kSampleRate = 64;

constexpr size_t kLimits[MemTracker::kBuckets - 1] = {16, 64, 256, 1024, 4096, 16384, 65536, 262144};
constexpr const char* kLabels[MemTracker::kBuckets] = {
    "<=16B", "<=64B", "<=256B", "<=1KB", "<=4KB", "<=16KB", "<=64KB", "<=256KB", ">256KB"};

int BucketFor(size_t size) {
    for (int i = 0; i < MemTracker::kBuckets - 1; ++i) {
        if (size <= kLimits[i]) return i;
    }
    return MemTracker::kBuckets - 1;
}

struct UnwindState {
    void** frames;
    size_t count;
};

_Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* context, void* argument) {
    auto* state = static_cast<UnwindState*>(argument);
    const uintptr_t ip = _Unwind_GetIP(context);
    if (ip != 0) {
        state->frames[state->count++] = reinterpret_cast<void*>(ip);
        if (state->count >= kMaxFrames) return _URC_END_OF_STACK;
    }
    return _URC_NO_REASON;
}

size_t CaptureBacktrace(void** out) {
    UnwindState state{out, 0};
    _Unwind_Backtrace(UnwindCallback, &state);
    return state.count;
}

uint64_t Tick() {
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

void WriteRecord(char type, void* ptr, size_t size, uint64_t ts, void* const* frames, size_t count) {
    if (!g_ready.load(std::memory_order_relaxed)) return;
    uint8_t buf[1 + 1 + 4 + 8 + 8 + kMaxFrames * 8];
    uint8_t* p = buf;
    *p++ = static_cast<uint8_t>(type);
    *p++ = static_cast<uint8_t>(count);
    const uint32_t size32 = static_cast<uint32_t>(size);
    __builtin_memcpy(p, &size32, 4); p += 4;
    const uint64_t ptr64 = reinterpret_cast<uint64_t>(ptr);
    __builtin_memcpy(p, &ptr64, 8); p += 8;
    __builtin_memcpy(p, &ts, 8); p += 8;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t frame = reinterpret_cast<uint64_t>(frames[i]);
        __builtin_memcpy(p, &frame, 8); p += 8;
    }
    const uint32_t length = static_cast<uint32_t>(p - buf);
    const uint32_t pos = g_ringPos.fetch_add(length, std::memory_order_relaxed);
    if (pos + length <= kRingSize) {
        __builtin_memcpy(g_ring + pos, buf, length);
    } else {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

// One flag per thread so a wrapped malloc from inside the tracker cannot recurse.
__thread bool t_inTracker = false;
__thread uint32_t t_allocSample = 0;
__thread uint32_t t_freeSample = 0;

void TrackAlloc(void* ptr) {
    if (ptr == nullptr || t_inTracker || !g_ready.load(std::memory_order_relaxed)) return;
    t_inTracker = true;
    const size_t size = malloc_usable_size(ptr);
    const int bucket = BucketFor(size);
    MemTracker::live_count[bucket].fetch_add(1, std::memory_order_relaxed);
    MemTracker::live_bytes[bucket].fetch_add(static_cast<int64_t>(size), std::memory_order_relaxed);
    const bool sample = size >= (256u << 10) || (++t_allocSample % kSampleRate) == 0;
    if (sample) {
        const uint64_t ts = Tick();
        void* frames[kMaxFrames];
        const size_t count = CaptureBacktrace(frames);
        WriteRecord('A', ptr, size, ts, frames, count);
    }
    t_inTracker = false;
}

void TrackFree(void* ptr) {
    if (ptr == nullptr || t_inTracker || !g_ready.load(std::memory_order_relaxed)) return;
    t_inTracker = true;
    const size_t size = malloc_usable_size(ptr);
    const int bucket = BucketFor(size);
    MemTracker::live_count[bucket].fetch_sub(1, std::memory_order_relaxed);
    MemTracker::live_bytes[bucket].fetch_sub(static_cast<int64_t>(size), std::memory_order_relaxed);
    if ((++t_freeSample % kSampleRate) == 0) {
        const uint64_t ts = Tick();
        void* frames[kMaxFrames];
        const size_t count = CaptureBacktrace(frames);
        WriteRecord('F', ptr, size, ts, frames, count);
    }
    t_inTracker = false;
}

} // namespace

namespace MemTracker {

void Init(const char* path) {
    if (g_ready.load(std::memory_order_relaxed)) return;
    if (path == nullptr) path = std::getenv("NSMBW_MEMTRACE_PATH");
    if (path == nullptr) path = "sdmc:/switch/nsmbw/memtrace.bin";
    std::snprintf(g_path, sizeof(g_path), "%s", path);
    g_ringPos.store(0, std::memory_order_relaxed);
    g_dropped.store(0, std::memory_order_relaxed);
    g_trace_anchor = reinterpret_cast<uint64_t>(&MemTracker::Init);
    g_ready.store(true, std::memory_order_release);
    RT_LOGF(RT_TAG_RUNTIME, "memtrace: capturing %zu MiB ring, sample 1/%u, dump at exit\n",
            kRingSize >> 20, kSampleRate);
}

void Close() {
    if (!g_ready.exchange(false, std::memory_order_acq_rel)) return;
    const uint32_t used = std::min<uint32_t>(g_ringPos.load(std::memory_order_relaxed), kRingSize);
    const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        RT_LOGF(RT_TAG_RUNTIME, "memtrace: cannot write %s\n", g_path);
        return;
    }
    const uint8_t header[8] = {'K', 'D', 'M', 'T', 2, 0, 0, 0};
    ::write(fd, header, sizeof(header));
    uint64_t anchor = g_trace_anchor;
    ::write(fd, &anchor, sizeof(anchor));
    size_t done = 0;
    while (done < used) {
        const ssize_t written = ::write(fd, g_ring + done, used - done);
        if (written <= 0) break;
        done += static_cast<size_t>(written);
    }
    ::close(fd);
    RT_LOGF(RT_TAG_RUNTIME, "memtrace: wrote %zu KiB, dropped %u records\n", done >> 10,
            g_dropped.load(std::memory_order_relaxed));
}

void LogHistogram() {
    const struct mallinfo mi = mallinfo();
    RT_LOGF(RT_TAG_RUNTIME, "memtrace: arena=%dKB used=%dKB free=%dKB chunks=%d ring=%uKB dropped=%u\n",
            mi.arena / 1024, mi.uordblks / 1024, mi.fordblks / 1024, mi.ordblks,
            g_ringPos.load(std::memory_order_relaxed) >> 10, g_dropped.load(std::memory_order_relaxed));
    for (int i = 0; i < kBuckets; ++i) {
        const int32_t count = live_count[i].load(std::memory_order_relaxed);
        const int64_t bytes = live_bytes[i].load(std::memory_order_relaxed);
        if (count != 0) {
            RT_LOGF(RT_TAG_RUNTIME, "memtrace: %s %d live, %lld KB\n", kLabels[i], count,
                    static_cast<long long>(bytes / 1024));
        }
    }
}

} // namespace MemTracker

extern "C" {
void* __real_malloc(size_t);
void __real_free(void*);
void* __real_realloc(void*, size_t);
void* __real_calloc(size_t, size_t);
void* __real_memalign(size_t, size_t);
int __real_posix_memalign(void**, size_t, size_t);
void* __real_aligned_alloc(size_t, size_t);

void* __wrap_malloc(size_t size) {
    void* ptr = __real_malloc(size);
    TrackAlloc(ptr);
    return ptr;
}

void __wrap_free(void* ptr) {
    TrackFree(ptr);
    __real_free(ptr);
}

void* __wrap_realloc(void* old, size_t size) {
    TrackFree(old);
    void* ptr = __real_realloc(old, size);
    TrackAlloc(ptr);
    return ptr;
}

void* __wrap_calloc(size_t count, size_t size) {
    void* ptr = __real_calloc(count, size);
    TrackAlloc(ptr);
    return ptr;
}

void* __wrap_memalign(size_t alignment, size_t size) {
    void* ptr = __real_memalign(alignment, size);
    TrackAlloc(ptr);
    return ptr;
}

int __wrap_posix_memalign(void** out, size_t alignment, size_t size) {
    const int result = __real_posix_memalign(out, alignment, size);
    if (result == 0) TrackAlloc(*out);
    return result;
}

void* __wrap_aligned_alloc(size_t alignment, size_t size) {
    void* ptr = __real_aligned_alloc(alignment, size);
    TrackAlloc(ptr);
    return ptr;
}
}

#endif
