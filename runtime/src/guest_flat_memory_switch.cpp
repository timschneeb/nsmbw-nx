#include "guest_flat_memory.h"

// Horizon has no mmap/mprotect/memfd_create/sigaction, so there is no guest
// view and no fault interception. Every translated access takes the checked
// Memory::* path (RequiresCheckedAccess is true below), which routes through
// HostPointer into these plain heap stores. Mirrors the main backend's store
// grouping so cached/uncached/physical mirrors keep aliasing.

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

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
        store.host = new uint8_t[store.size]();
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
