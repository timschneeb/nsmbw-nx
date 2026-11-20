#pragma once

#if defined(NSMBW_MEMTRACE)

#include <atomic>
#include <cstddef>
#include <cstdint>

// Heap allocation trace, ported from kdeconnect-nx's mem_tracker. Records every malloc/free
// through --wrap hooks with a short frame-pointer backtrace. Opt-in: configure with
// -DMKW_MEMTRACE=ON. The trace format is the one tools/parse_memtrace.py reads.
namespace MemTracker {

constexpr int kBuckets = 9;
extern std::atomic<int32_t> live_count[kBuckets];
extern std::atomic<int64_t> live_bytes[kBuckets];

// Opens the trace file. A null path uses NSMBW_MEMTRACE_PATH or
// sdmc:/switch/nsmbw/memtrace.bin. The byte cap comes from NSMBW_MEMTRACE_MAX_MB (512 MiB default).
void Init(const char* path = nullptr);
void Close();
void LogHistogram();

} // namespace MemTracker

#endif
