// Fault-driven guest MMIO loads (Switch).
//
// Translated code reaches guest memory as `[x28 + addr]`, so an MMIO load
// (0xCC000000-0xCDFFFFFF) hits an uncommitted page of the flat window once
// FlatReadNeedsPolicy is compiled out. fastmem_entry.S catches that data abort,
// saves the whole faulting context and calls NsmbwFastmemFault here, which
// decodes the load and fetches the value through Memory::Read*, so the GX/DSP
// MMIO policy stays in one place.
//
// Nothing here may allocate or lock: this runs on the exception path.

#include <switch.h>

#include <cstddef>
#include <cstdint>

#include "memory_access.h"

namespace {

// Layout of one save slot in fastmem_entry.S; keep in sync with its .equ values.
struct FastmemSlot {
    uint64_t q[64];  // q0-q31, two 64-bit halves each
    uint64_t x[31];  // x0-x30
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint64_t frame;  // ThreadExceptionFrameA64 *
    uint64_t esr;
    uint64_t far;  // faulting host address in the flat window
};
static_assert(offsetof(FastmemSlot, x) == 0x200, "slot x offset");
static_assert(offsetof(FastmemSlot, sp) == 0x2F8, "slot sp offset");
static_assert(offsetof(FastmemSlot, pc) == 0x300, "slot pc offset");
static_assert(offsetof(FastmemSlot, pstate) == 0x308, "slot pstate offset");
static_assert(offsetof(FastmemSlot, frame) == 0x310, "slot frame offset");
static_assert(offsetof(FastmemSlot, esr) == 0x318, "slot esr offset");
static_assert(offsetof(FastmemSlot, far) == 0x320, "slot far offset");
static_assert(sizeof(FastmemSlot) <= 0x400, "slot header must fit its reserved half page");

// The frame offsets fastmem_entry.S hard codes, verified against libnx's header.
static_assert(offsetof(ThreadExceptionFrameA64, lr) == 0x48, "frame lr offset");
static_assert(offsetof(ThreadExceptionFrameA64, sp) == 0x50, "frame sp offset");
static_assert(offsetof(ThreadExceptionFrameA64, elr_el1) == 0x58, "frame pc offset");
static_assert(offsetof(ThreadExceptionFrameA64, pstate) == 0x60, "frame pstate offset");
static_assert(offsetof(ThreadExceptionFrameA64, esr) == 0x6C, "frame esr offset");
static_assert(offsetof(ThreadExceptionFrameA64, far) == 0x70, "frame far offset");
static_assert(sizeof(ThreadExceptionFrameA64) == 0x78, "frame size");

constexpr uint64_t kMmioBegin = 0xCC000000ull;
constexpr uint64_t kMmioEnd = 0xCE000000ull;

// ESR_EL1: EC[31:26] = 0x24 for a data abort from a lower exception level and
// ISS[6] (WnR) = 0 for a read.
constexpr uint64_t kEsrEcMask = 0x3Full << 26;
constexpr uint64_t kEsrEcDataAbortLowerEl = 0x24ull << 26;
constexpr uint64_t kEsrWnR = 1ull << 6;

// AArch64 load/store register family: bits 29-27 are 111 for the scalar and
// SIMD&FP loads this handles.
constexpr uint32_t kLoadStoreRegisterMask = 0x38000000u;

enum class Writeback { None, Post, Pre };

struct LoadSpec {
    uint32_t rt = 0;
    uint32_t rn = 0;
    uint32_t width = 0;
    bool vector = false;
    bool signExtend = false;
    bool wide = false;
    Writeback writeback = Writeback::None;
    int64_t offset = 0;
};

bool TryGuestAddress(uint64_t far, uint32_t& guest) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GuestFlat::FlatGuestBase());
    if (base == 0 || far < base) {
        return false;
    }
    const uint64_t offset = far - base;
    if (offset > UINT32_MAX) {
        return false;
    }
    guest = static_cast<uint32_t>(offset);
    return true;
}

bool DecodeLoad(uint32_t insn, LoadSpec& spec) {
    if ((insn & kLoadStoreRegisterMask) != kLoadStoreRegisterMask) {
        return false;
    }
    const uint32_t size = insn >> 30;
    const uint32_t vector = (insn >> 26) & 1u;
    const uint32_t mode = (insn >> 24) & 3u;
    const uint32_t opc = (insn >> 22) & 3u;

    spec.rt = insn & 0x1Fu;
    spec.rn = (insn >> 5) & 0x1Fu;
    spec.vector = vector != 0;

    switch (mode) {
    case 0:  // unscaled, post-index, unprivileged or pre-index
        if ((insn >> 21) & 1u) {
            // Register offset: Rm/option/shift in bits 20-10.
            if (((insn >> 10) & 3u) != 2u) {
                return false;
            }
            break;
        }
        {
            const uint32_t kind = (insn >> 10) & 3u;
            if (kind == 2u) {
                return false;  // unprivileged
            }
            if (kind != 0u) {
                const uint32_t imm9 = (insn >> 12) & 0x1FFu;
                spec.writeback = kind == 1u ? Writeback::Post : Writeback::Pre;
                spec.offset = static_cast<int64_t>(static_cast<int32_t>(imm9 << 23) >> 23);
            }
        }
        break;
    case 1:  // unsigned immediate
        break;
    case 2:  // register offset
        if (((insn >> 10) & 3u) != 2u) {
            return false;
        }
        break;
    default:
        return false;
    }

    if (spec.vector) {
        if (opc == 0u) {
            return false;  // store
        }
        if (opc == 1u) {
            spec.width = 1u << size;  // B, H, S or D
            return true;
        }
        if (opc == 3u && size == 0u) {
            spec.width = 16;  // Q
            return true;
        }
        return false;
    }

    if (opc == 0u) {
        return false;  // store
    }
    spec.width = 1u << size;
    if (opc == 1u) {
        return true;  // zero-extending LDRB/LDRH/LDR/LDR
    }
    // opc 2 sign-extends into a 64-bit register (LDRSB/LDRSH/LDRSW), opc 3 into
    // a 32-bit one (LDRSB/LDRSH Wt); the remaining size values are PRFM.
    if (opc == 2u) {
        if (size == 3u) {
            return false;
        }
        spec.signExtend = true;
        spec.wide = true;
        return true;
    }
    if (size >= 2u) {
        return false;
    }
    spec.signExtend = true;
    return true;
}

// The translator's flat loads read raw little-endian bytes and swap afterwards
// (MaybeByteSwap for FlatLoad<T>, vrev64_u8 for the paired-PSQ path), so the
// emulated load must deliver those raw bytes, not the guest-order value.
uint64_t ReadRaw(uint32_t guest, uint32_t width) {
    switch (width) {
    case 1:
        return Memory::Read8(guest);
    case 2:
        return __builtin_bswap16(Memory::Read16(guest));
    case 4:
        return __builtin_bswap32(Memory::Read32(guest));
    default:
        return __builtin_bswap64(Memory::Read64(guest));
    }
}

uint64_t Extend(uint64_t raw, const LoadSpec& spec) {
    if (!spec.signExtend) {
        return raw;
    }
    if (spec.wide) {
        switch (spec.width) {
        case 1:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(raw)));
        case 2:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(raw)));
        default:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(raw)));
        }
    }
    switch (spec.width) {
    case 1:
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
    case 2:
        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
    default:
        return static_cast<uint32_t>(raw);
    }
}

char g_faultLine[160];

size_t AppendText(size_t pos, const char* text) {
    while (*text != '\0' && pos < sizeof(g_faultLine) - 1) {
        g_faultLine[pos++] = *text++;
    }
    return pos;
}

size_t AppendHex(size_t pos, uint64_t value, unsigned digits) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (unsigned i = 0; i < digits; ++i) {
        g_faultLine[pos++] = kDigits[(value >> ((digits - 1u - i) * 4u)) & 0xFu];
    }
    return pos;
}

}  // namespace

extern "C" uintptr_t NsmbwFastmemFault(uint64_t far, uint64_t esr, uint64_t pc, uint64_t* gprs) {
    if ((esr & kEsrEcMask) != kEsrEcDataAbortLowerEl || (esr & kEsrWnR) != 0) {
        return 0;
    }
    uint32_t guest = 0;
    if (!TryGuestAddress(far, guest) || guest < kMmioBegin || guest >= kMmioEnd) {
        return 0;
    }

    LoadSpec spec;
    if (!DecodeLoad(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(pc)), spec)) {
        return 0;
    }

    auto* slot = reinterpret_cast<FastmemSlot*>(reinterpret_cast<uint8_t*>(gprs) -
                                                offsetof(FastmemSlot, x));
    try {
        if (spec.vector) {
            if (spec.width == 16) {
                slot->q[spec.rt * 2] = ReadRaw(guest, 8);
                slot->q[spec.rt * 2 + 1] = ReadRaw(guest + 8, 8);
            } else {
                slot->q[spec.rt * 2] = ReadRaw(guest, spec.width);
                slot->q[spec.rt * 2 + 1] = 0;
            }
        } else {
            const uint64_t raw = ReadRaw(guest, spec.width);
            if (spec.rt != 31) {
                slot->x[spec.rt] = Extend(raw, spec);
            }
        }
    } catch (...) {
        // Memory::Read* reports MMIO reads that need HLE by throwing; the caller
        // prints far/pc/esr and exits instead of resuming with a stale register.
        return 0;
    }

    if (spec.writeback != Writeback::None) {
        if (spec.rn == 31) {
            return 0;
        }
        slot->x[spec.rn] = spec.writeback == Writeback::Post ? far + spec.offset : far;
    }
    return static_cast<uintptr_t>(pc + 4);
}

extern "C" [[noreturn]] void NsmbwFastmemUnhandled(uint64_t far, uint64_t esr, uint64_t pc) {
    uint32_t guest = 0;
    const uint64_t shown = TryGuestAddress(far, guest) ? guest : far;

    size_t pos = AppendText(0, "[fastmem] unhandled fault far=0x");
    pos = AppendHex(pos, shown, 16);
    pos = AppendText(pos, " pc=0x");
    pos = AppendHex(pos, pc, 16);
    pos = AppendText(pos, " esr=0x");
    pos = AppendHex(pos, esr & 0xFFFFFFFFull, 8);
    g_faultLine[pos++] = '\n';

    svcOutputDebugString(g_faultLine, pos);
    svcExitProcess();
    for (;;) {
    }
}
