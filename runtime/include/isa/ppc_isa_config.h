#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#define MKW_RESTRICT __restrict
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#else
#error "ppc_isa_config.h has no SIMD intrinsics header for this architecture"
#endif

inline constexpr bool MkwStateFreeAbiEnabled(uint32_t) noexcept
{
    return true;
}

#if defined(_WIN32)
#define MKW_PPC_FORCE_INLINE __forceinline
#define MKW_PPC_NO_INLINE __declspec(noinline)
#define MKW_PPC_INTERNAL_CALL __regcall
#else
// __forceinline/__declspec are MS-extension keywords Clang only recognizes when targeting
// Windows (MSVC or mingw); native Linux Clang needs the GNU-attribute spellings instead.
// __regcall has no portable non-Windows equivalent worth chasing here - the extra register
// args it saves matter for the hot PPC interpreter loop on Windows, but plain calls are fine
// elsewhere.
#define MKW_PPC_FORCE_INLINE __attribute__((always_inline)) inline
#define MKW_PPC_NO_INLINE __attribute__((noinline))
#define MKW_PPC_INTERNAL_CALL
#endif

#if defined(_MSC_VER)
#define MKW_PPC_UNREACHABLE() __assume(0)
#else
#define MKW_PPC_UNREACHABLE() __builtin_unreachable()
#endif

#define MKW_PPC_ALWAYS_INLINE_BODY __attribute__((always_inline))
#define MKW_PPC_COLD __attribute__((cold))


#if defined(__clang__)
using MkwStateFreeResult2 = uint64_t __attribute__((ext_vector_type(2)));
#else
// GCC has no ext_vector_type, and rejects return braced-init on vector_size
// vectors. This struct covers every operation translated code performs on
// the type: return {a, b}, copy, [0]/[1] subscript, static_cast to u32/u64
// (lane 0, matching Clang's vector-to-scalar conversion).
struct MkwStateFreeResult2 {
    uint64_t v[2];
    constexpr uint64_t operator[](std::size_t i) const noexcept { return v[i]; }
    constexpr uint64_t& operator[](std::size_t i) noexcept { return v[i]; }
    constexpr explicit operator uint32_t() const noexcept {
        return static_cast<uint32_t>(v[0]);
    }
    constexpr explicit operator uint64_t() const noexcept { return v[0]; }
};
#endif
