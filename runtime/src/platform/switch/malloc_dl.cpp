// Replaces newlib's allocator with dlmalloc 2.8.6 on Switch.
//
// newlib's malloc is a single free list with no coalescing policy worth the
// name; the runtime allocates per frame and during level loads, so the
// allocator's small-allocation path is on the guest's critical path. dlmalloc
// keeps segregated bins and coalesces on free.
//
// newlib routes malloc/free/realloc/calloc/memalign through the _*_r entry
// points, so overriding those covers the whole process, libnx included.

#include <malloc.h>
#include <reent.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>

// libnx's headers are C; a C++ TU would mangle mutexLock/mutexUnlock.
extern "C" {
#include <switch/kernel/mutex.h>
}

static inline int InitialLock(Mutex* lock) {
    mutexInit(lock);
    return 0;
}

static inline int AcquireLock(Mutex* lock) {
    mutexLock(lock);
    return 0;
}

#define USE_LOCKS 2
#define MLOCK_T Mutex
#define INITIAL_LOCK(lk) InitialLock(lk)
#define DESTROY_LOCK(lk) (0)
#define ACQUIRE_LOCK(lk) AcquireLock(lk)
#define RELEASE_LOCK(lk) mutexUnlock(lk)
#define TRY_LOCK(lk) mutexTryLock(lk)
static MLOCK_T malloc_global_mutex = 0;

#undef M_TRIM_THRESHOLD
#undef M_MMAP_THRESHOLD

#define USE_DL_PREFIX 1
#define STRUCT_MALLINFO_DECLARED 1
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define MORECORE_CONTIGUOUS 1
#define MORECORE_CANNOT_TRIM 1
#define DEFAULT_GRANULARITY ((size_t)1 << 20)
#define malloc_getpagesize ((size_t)0x1000)
#define LACKS_TIME_H 1
#define LACKS_SYS_MMAN_H 1
#define REALLOC_ZERO_BYTES_FREES 1

#include "../../../third_party/dlmalloc/malloc.c"

// dlmallinfo() walks every chunk in the arena, and the global allocator lock is held for the
// walk: on a 1 GB arena that is a visible frame spike. Track live bytes instead, which makes the
// free-space query a load plus dlmalloc_footprint() (O(1), no lock).
// Signed so a stray free without a tracked allocation cannot wrap into a huge used value.
static std::atomic<ptrdiff_t> g_usedBytes{0};

static void note_alloc(void* ptr) {
    if (ptr != nullptr) {
        g_usedBytes.fetch_add(dlmalloc_usable_size(ptr), std::memory_order_relaxed);
    }
}

void* _malloc_r(struct _reent* reent, size_t size) {
    (void)reent;
    void* ptr = dlmalloc(size);
    note_alloc(ptr);
    return ptr;
}

void _free_r(struct _reent* reent, void* ptr) {
    (void)reent;
    if (ptr != nullptr) {
        g_usedBytes.fetch_sub(dlmalloc_usable_size(ptr), std::memory_order_relaxed);
    }
    dlfree(ptr);
}

void* _realloc_r(struct _reent* reent, void* ptr, size_t size) {
    (void)reent;
    const size_t before = ptr != nullptr ? dlmalloc_usable_size(ptr) : 0;
    void* result = dlrealloc(ptr, size);
    if (result != nullptr) {
        g_usedBytes.fetch_add(dlmalloc_usable_size(result), std::memory_order_relaxed);
        g_usedBytes.fetch_sub(before, std::memory_order_relaxed);
    }
    return result;
}

void* _calloc_r(struct _reent* reent, size_t count, size_t size) {
    (void)reent;
    void* ptr = dlcalloc(count, size);
    note_alloc(ptr);
    return ptr;
}

void* _memalign_r(struct _reent* reent, size_t alignment, size_t size) {
    (void)reent;
    void* ptr = dlmemalign(alignment, size);
    note_alloc(ptr);
    return ptr;
}

void* _valloc_r(struct _reent* reent, size_t size) {
    (void)reent;
    void* ptr = dlvalloc(size);
    note_alloc(ptr);
    return ptr;
}

void* _pvalloc_r(struct _reent* reent, size_t size) {
    (void)reent;
    void* ptr = dlpvalloc(size);
    note_alloc(ptr);
    return ptr;
}

size_t _malloc_usable_size_r(struct _reent* reent, void* ptr) {
    (void)reent;
    return dlmalloc_usable_size(ptr);
}

struct mallinfo _mallinfo_r(struct _reent* reent) {
    (void)reent;
    return dlmallinfo();
}

int _malloc_trim_r(struct _reent* reent, size_t pad) {
    (void)reent;
    return dlmalloc_trim(pad);
}

int _mallopt_r(struct _reent* reent, int parameter, int value) {
    (void)reent;
    return dlmallopt(parameter, value);
}

void _malloc_stats_r(struct _reent* reent) {
    (void)reent;
    dlmalloc_stats();
}

extern "C" size_t NsmbwMallocUsedBytes() {
    const ptrdiff_t used = g_usedBytes.load(std::memory_order_relaxed);
    return used > 0 ? static_cast<size_t>(used) : 0;
}

extern "C" size_t NsmbwMallocArenaBytes() {
    return dlmalloc_footprint();
}

extern "C" size_t NsmbwMallocFreeBytes() {
    const size_t footprint = dlmalloc_footprint();
    const ptrdiff_t used = g_usedBytes.load(std::memory_order_relaxed);
    if (used <= 0) {
        return footprint;
    }
    const size_t usedBytes = static_cast<size_t>(used);
    return footprint > usedBytes ? footprint - usedBytes : 0;
}
