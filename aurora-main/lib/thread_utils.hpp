#pragma once

#include <cstdint>
#include <thread>

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>
#endif

namespace aurora {

// Switch exposes three application cores (0-2). libstdc++ reports zero here.
inline uint32_t hardware_thread_count() noexcept {
#if defined(MKW_PLATFORM_SWITCH)
  return 3;
#else
  return std::thread::hardware_concurrency();
#endif
}

inline bool pin_current_thread(uint32_t core) noexcept {
#if defined(MKW_PLATFORM_SWITCH)
  if (core > 2) {
    return false;
  }
  const Result rc = svcSetThreadCoreMask(threadGetCurHandle(), static_cast<s32>(core), 1ull << core);
  return R_SUCCEEDED(rc);
#else
  (void)core;
  return true;
#endif
}

} // namespace aurora
