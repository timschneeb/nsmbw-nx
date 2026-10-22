#include "guest_flat_memory.h"
#include "runtime_log.h"

// Horizon has no mmap/mprotect/memfd_create/sigaction, so there is no guest
// view and no fault interception. Guest RAM is aliased into one reserved
// 4 GiB code-memory window instead (the SwitchWakerHD/Cemu-nx recipe), so a
// guest address equals host window + address and the checked path's page-bias
// table points straight at it. Mirrors the main backend's store grouping so
// cached/uncached/physical mirrors keep aliasing.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>
#endif

namespace GuestFlat {
namespace {
struct Mapping {
    uint32_t base;
    uint64_t size;
    uint8_t* host;
};
struct Store {
    Backing kind = Backing::Owned;
    uint32_t owned = 0;
    uint64_t size = 0;
    uint8_t* host = nullptr;
};
std::mutex g_mutex;
std::vector<Mapping> g_mappings;
std::vector<Store> g_stores;
std::vector<RegionRequest> g_layout;
bool g_active = false;

constexpr uint64_t kStoreGranularity = 0x10000u;  // 64 KiB, same as the main backend

#if defined(MKW_PLATFORM_SWITCH)
// One 4 GiB window carries every guest view. It is reserved up front because
// Horizon's code-memory region fragments once other services start mapping.
constexpr uint64_t kWindowSize = 0x100000000ull;
uint8_t* g_window = nullptr;

bool ReserveGuestWindow() {
    // Both syscalls are hinted only when launched from hbmenu over a game with
    // full memory access, which RuntimeMain already enforces.
    if (!envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x73)) {
        return false;
    }
    virtmemLock();
    void* window = virtmemFindCodeMemory(kWindowSize, 0x10000);
    if (window != nullptr) {
        virtmemAddReservation(window, kWindowSize);
    }
    virtmemUnlock();
    g_window = static_cast<uint8_t*>(window);
    return g_window != nullptr;
}

uint64_t StoreGuestBase(const Store& store) {
    switch (store.kind) {
    case Backing::Mem1:
        return 0x80000000u;
    case Backing::Mem2:
        return 0x90000000u;
    case Backing::Owned:
    default:
        return store.owned;
    }
}

// Allocate the store's pages and alias them at window + guest base, so guest address ==
// host address. Only the primary view is mapped: Horizon rejects a second code-memory
// alias of the same page, so mirror views take the checked access path instead.
bool CommitStore(Store& store) {
    const uint64_t guestBase = StoreGuestBase(store);
    if (g_window == nullptr || guestBase + store.size > kWindowSize) {
        return false;
    }
    void* backing = std::aligned_alloc(0x1000, store.size);
    if (backing == nullptr) {
        return false;
    }
    std::memset(backing, 0, store.size);
    const u64 target = reinterpret_cast<u64>(g_window + guestBase);
    const Handle self = envGetOwnProcessHandle();
    const Result mapResult =
        svcMapProcessCodeMemory(self, target, reinterpret_cast<u64>(backing), store.size);
    if (R_FAILED(mapResult)) {
        RT_LOG(RT_TAG_MEMORY) << "[mem] map store at 0x" << std::hex << guestBase << " size 0x"
                              << store.size << " failed: 0x" << mapResult << std::dec << std::endl;
        std::free(backing);
        return false;
    }
    const Result permResult = svcSetProcessMemoryPermission(self, target, store.size, Perm_Rw);
    if (R_FAILED(permResult)) {
        RT_LOG(RT_TAG_MEMORY) << "[mem] perm store at 0x" << std::hex << guestBase << " failed: 0x"
                              << permResult << std::dec << std::endl;
        std::free(backing);
        return false;
    }
    store.host = g_window + guestBase;
    return true;
}
#endif

uint64_t Offset(const RegionRequest& region) {
    switch (region.backing) {
    case Backing::Mem1:
        return region.base & 0x01FFFFFFu;  // 32 MiB MEM1 window
    case Backing::Mem2:
        return region.base & 0x0FFFFFFFu;  // 256 MiB MEM2 window
    case Backing::Owned:
    default:
        return 0;
    }
}

bool Same(const std::vector<RegionRequest>& a, const std::vector<RegionRequest>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.base == y.base && x.size == y.size && x.backing == y.backing;
           });
}
}  // namespace

bool IsActive() { return g_active; }

#if defined(MKW_PLATFORM_SWITCH)
// Translated code indexes guest RAM as base + address once the window exists.
uint8_t* g_flatGuestBase = nullptr;
#endif

void Initialize(const std::vector<RegionRequest>& regions) {
    std::lock_guard lock(g_mutex);
    if (g_active) {
        if (!Same(g_layout, regions)) {
            throw std::runtime_error("flat guest layout cannot be remapped");
        }
        for (auto& store : g_stores) {
            std::fill(store.host, store.host + store.size, uint8_t{0});
        }
        return;
    }
    for (const auto& region : regions) {
        if (region.size == 0) {
            continue;
        }
        if ((region.base % kStoreGranularity) != 0) {
            throw std::runtime_error("guest region base is not 64 KiB aligned");
        }
        const uint32_t owned = region.backing == Backing::Owned ? region.base : 0;
        auto it = std::find_if(g_stores.begin(), g_stores.end(), [&](const Store& store) {
            return store.kind == region.backing && store.owned == owned;
        });
        const uint64_t need = Offset(region) + region.size;
        if (it == g_stores.end()) {
            g_stores.push_back({region.backing, owned, need, nullptr});
        } else {
            it->size = std::max(it->size, need);
        }
    }
    for (auto& store : g_stores) {
        store.size = (store.size + kStoreGranularity - 1) & ~(kStoreGranularity - 1);
    }
#if defined(MKW_PLATFORM_SWITCH)
    if (ReserveGuestWindow()) {
        SetFlatGuestBase(g_window);
        uint64_t backed = 0;
        for (auto& store : g_stores) {
            if (CommitStore(store)) {
                backed += store.size;
            }
        }
        RT_LOG(RT_TAG_MEMORY) << "[mem] guest window at " << static_cast<void*>(g_window) << ", "
                              << (backed >> 20) << " MiB backed" << std::endl;
    } else {
        RT_LOG(RT_TAG_MEMORY) << "[mem] guest window reservation failed" << std::endl;
        // Checked access is off on Switch, so a heap fallback would still hand
        // translated code window addresses. Refuse instead of dying later.
        throw std::runtime_error(
            "guest window reservation failed; launch from hbmenu with full memory");
    }
#endif
    for (auto& store : g_stores) {
        if (store.host == nullptr) {
            store.host = new uint8_t[store.size]();
        }
    }
    for (const auto& region : regions) {
        if (region.size == 0) {
            continue;
        }
        const uint32_t owned = region.backing == Backing::Owned ? region.base : 0;
        const auto& store = *std::find_if(g_stores.begin(), g_stores.end(), [&](const Store& s) {
            return s.kind == region.backing && s.owned == owned;
        });
        g_mappings.push_back({region.base, region.size, store.host + Offset(region)});
    }
    g_layout = regions;
    g_active = true;
}

uint8_t* HostPointer(uint32_t address) {
    for (const auto& mapping : g_mappings) {
        if (address >= mapping.base && static_cast<uint64_t>(address - mapping.base) < mapping.size) {
            return mapping.host + (address - mapping.base);
        }
    }
    return nullptr;
}

void ProtectDeferredRange(uint32_t, size_t) {}
void UnprotectDeferredRange(uint32_t, size_t) {}
void RegisterExecutableRange(uint32_t, uint32_t) {}
FaultCounters Counters() { return {}; }
void LogFaultSummary() noexcept {}
bool HandleAccessViolation(void*, bool) noexcept { return false; }
}  // namespace GuestFlat
