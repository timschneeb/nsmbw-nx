#pragma once
// Inline access to the few thread_locals on the guest hot path.
//
// devkitA64 builds with -mtp=soft, so GCC emits a call to __aarch64_read_tp for every
// thread_local access. libnx's helper reads tpidrro_el0, loads the ELF thread pointer from the
// last field of its ThreadVars and returns, and GCC treats the call as volatile, so it is repeated
// for every access. The macro below emits the same two instructions and then adds the variable's
// local-exec offset with the relocation pair GCC uses for -ftls-model=local-exec.

#include <cstdint>

#if defined(__SWITCH__) && defined(__aarch64__) && !defined(MKW_SWITCH_SOFT_TP_ONLY)
#define MKW_SWITCH_INLINE_TP 1

// libnx keeps its ThreadVars in the last 0x20 bytes of the 0x200-byte tpidrro_el0 region; the
// final field is the ELF TLS thread pointer. Same offset as __aarch64_read_tp.
inline constexpr uintptr_t kMkwNxThreadVarsTlsTpOffset = 0x1F8;

__attribute__((always_inline)) inline uintptr_t MkwSwitchThreadPointer() noexcept {
    uintptr_t region;
    // volatile: never reuse a thread pointer across a call, exactly like the soft-tp helper. A
    // host fiber must not carry one thread's TLS address into another.
    __asm__ __volatile__("mrs %0, tpidrro_el0" : "=r"(region));
    return *reinterpret_cast<const uintptr_t*>(region + kMkwNxThreadVarsTlsTpOffset);
}

// `asm_symbol` is the variable's assembler name: unmangled in the global namespace, the
// Itanium-mangled name otherwise.
#define MKW_HOT_TLS_ACCESSOR(accessor, type, variable, asm_symbol)                        \
    __attribute__((always_inline)) inline type& accessor() noexcept                       \
    {                                                                                     \
        uintptr_t address;                                                                \
        __asm__("add %0, %1, #:tprel_hi12:" asm_symbol ", lsl #12\n\t"                     \
                "add %0, %0, #:tprel_lo12_nc:" asm_symbol                                  \
                : "=r"(address)                                                           \
                : "r"(MkwSwitchThreadPointer()));                                         \
        return *reinterpret_cast<type*>(address);                                         \
    }
#else
#define MKW_HOT_TLS_ACCESSOR(accessor, type, variable, asm_symbol)                        \
    inline type& accessor() noexcept { return variable; }
#endif

// Compares every accessor with the compiler's own &variable once at startup. A mismatch means the
// assembler name or the libnx offset is wrong, so the run must stop instead of using bad memory.
bool VerifyHotTlsAccessors() noexcept;
