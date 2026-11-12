// Fault-driven guest MMIO accesses (Switch).
//
// Translated code reaches guest memory as `[x28 + addr]`, so an MMIO access
// (0xCC000000-0xCDFFFFFF) hits an uncommitted page of the flat window once
// FlatReadNeedsPolicy/FlatWriteNeedsPolicy are compiled out. fastmem_entry.S catches that
// data abort, saves the whole faulting context and calls NsmbwFastmemFault here, which
// decodes the load or store and runs the value through Memory::Read*/Write*, so the GX/DSP
// MMIO policy stays in one place. A store's value is still in the faulting register, and the
// translator's raw store bytes are converted back to guest order here.
//
// Nothing here may allocate or lock: this runs on the exception path.

#include <switch.h>

#include <cstddef>
#include <cstdint>

#include "memory_access.h"
#include "perf_stats.h"

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
// ISS[6] (WnR) = 1 for a write.
constexpr uint64_t kEsrEcMask = 0x3Full << 26;
constexpr uint64_t kEsrEcDataAbortLowerEl = 0x24ull << 26;
constexpr uint64_t kEsrWnR = 1ull << 6;

enum class Writeback { None, Post, Pre };

struct AccessSpec {
    uint32_t rt = 0;
    uint32_t rt2 = 0;
    uint32_t rn = 0;
    uint32_t width = 0;
    bool vector = false;
    bool store = false;
    bool pair = false;
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

// Addressing shared by every form: the faulting address itself is `far`, so only the writeback
// and the post-index amount matter here. Rm-based forms carry no writeback.
bool DecodeAddressing(uint32_t insn, AccessSpec& spec) {
    const uint32_t mode = (insn >> 24) & 3u;
    spec.rn = (insn >> 5) & 0x1Fu;
    switch (mode) {
    case 0:  // unscaled, post-index, unprivileged or pre-index
        if ((insn >> 21) & 1u) {
            // Register offset: Rm/option/shift in bits 20-10.
            return ((insn >> 10) & 3u) == 2u;
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
        return true;
    case 1:  // unsigned immediate
        return true;
    case 2:  // register offset
        return ((insn >> 10) & 3u) == 2u;
    default:
        return false;
    }
}

bool DecodeSingle(uint32_t insn, AccessSpec& spec) {
    const uint32_t size = insn >> 30;
    const uint32_t vector = (insn >> 26) & 1u;
    const uint32_t opc = (insn >> 22) & 3u;

    spec.rt = insn & 0x1Fu;
    spec.vector = vector != 0;
    if (!DecodeAddressing(insn, spec)) {
        return false;
    }

    if (spec.vector) {
        if (opc == 0u) {
            spec.store = true;
            spec.width = 1u << size;  // B, H, S or D
            return true;
        }
        if (opc == 1u) {
            spec.width = 1u << size;
            return true;
        }
        if (opc == 2u && size == 0u) {
            spec.store = true;
            spec.width = 16;  // Q
            return true;
        }
        if (opc == 3u && size == 0u) {
            spec.width = 16;  // Q
            return true;
        }
        return false;
    }

    if (opc == 0u) {
        spec.store = true;
        spec.width = 1u << size;
        return true;
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

bool DecodePair(uint32_t insn, AccessSpec& spec) {
    const uint32_t size = insn >> 30;
    const uint32_t vector = (insn >> 26) & 1u;
    const uint32_t kind = (insn >> 23) & 3u;
    const uint32_t load = (insn >> 22) & 1u;

    spec.rt = insn & 0x1Fu;
    spec.rn = (insn >> 5) & 0x1Fu;
    spec.rt2 = (insn >> 10) & 0x1Fu;
    spec.vector = vector != 0;
    spec.pair = true;
    spec.store = load == 0;

    if (spec.vector) {
        switch (size) {
        case 0: spec.width = 4; break;   // S
        case 1: spec.width = 8; break;   // D
        case 2: spec.width = 16; break;  // Q
        default: return false;
        }
    } else {
        switch (size) {
        case 0: spec.width = 4; break;   // W
        case 2: spec.width = 8; break;   // X
        default: return false;
        }
    }

    if (kind == 1u) {
        spec.writeback = Writeback::Post;
    } else if (kind == 3u) {
        spec.writeback = Writeback::Pre;
    }
    const uint32_t imm7 = (insn >> 15) & 0x7Fu;
    spec.offset = static_cast<int64_t>(static_cast<int32_t>(imm7 << 25) >> 25) *
                  static_cast<int64_t>(spec.width);
    return true;
}

bool DecodeAccess(uint32_t insn, AccessSpec& spec) {
    const uint32_t family = (insn >> 27) & 0x7u;
    if (family == 0x7u) {
        return DecodeSingle(insn, spec);
    }
    if (family == 0x5u) {
        return DecodePair(insn, spec);
    }
    return false;
}

// The translator's flat stores write raw little-endian bytes (MaybeByteSwap of the guest value),
// so a store fault hands back the byte-swapped value. Undo it before the Memory::Write* policy.
uint64_t GuestOrderStore(uint64_t raw, uint32_t width) {
    switch (width) {
    case 1:
        return raw & 0xFFu;
    case 2:
        return __builtin_bswap16(static_cast<uint16_t>(raw));
    case 4:
        return __builtin_bswap32(static_cast<uint32_t>(raw));
    default:
        return __builtin_bswap64(raw);
    }
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

uint64_t Extend(uint64_t raw, const AccessSpec& spec) {
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

bool EmitLoad(FastmemSlot& slot, const AccessSpec& spec, uint32_t reg, uint32_t guest) {
    if (spec.width == 16) {
        if (!spec.vector) {
            return false;
        }
        slot.q[reg * 2] = ReadRaw(guest, 8);
        slot.q[reg * 2 + 1] = ReadRaw(guest + 8, 8);
        return true;
    }
    const uint64_t raw = ReadRaw(guest, spec.width);
    if (spec.vector) {
        slot.q[reg * 2] = raw;
        slot.q[reg * 2 + 1] = 0;
        return true;
    }
    if (reg != 31) {
        slot.x[reg] = Extend(raw, spec);
    }
    return true;
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

// One register's bytes, as the store would have put them, through the same Memory::Write* policy
// the inline test used to call. GPR 31 is XZR, which stores zero; vector 31 is a real register.
bool EmitStore(const FastmemSlot& slot, uint32_t reg, bool vector, uint32_t width, uint32_t guest) {
    if (width == 16) {
        if (!vector) {
            return false;
        }
        Memory::Write64(guest, GuestOrderStore(slot.q[reg * 2], 8));
        Memory::Write64(guest + 8, GuestOrderStore(slot.q[reg * 2 + 1], 8));
        return true;
    }
    const uint64_t raw = vector ? slot.q[reg * 2] : (reg == 31 ? 0 : slot.x[reg]);
    switch (width) {
    case 1:
        Memory::Write8(guest, static_cast<uint8_t>(raw));
        return true;
    case 2:
        Memory::Write16(guest, static_cast<uint16_t>(GuestOrderStore(raw, 2)));
        return true;
    case 4:
        Memory::Write32(guest, static_cast<uint32_t>(GuestOrderStore(raw, 4)));
        return true;
    case 8:
        Memory::Write64(guest, GuestOrderStore(raw, 8));
        return true;
    default:
        return false;
    }
}

}  // namespace

extern "C" uintptr_t NsmbwFastmemFault(uint64_t far, uint64_t esr, uint64_t pc, uint64_t* gprs) {
    if ((esr & kEsrEcMask) != kEsrEcDataAbortLowerEl) {
        return 0;
    }
    const bool isWrite = (esr & kEsrWnR) != 0;
    uint32_t guest = 0;
    if (!TryGuestAddress(far, guest) || guest < kMmioBegin || guest >= kMmioEnd) {
        return 0;
    }

    AccessSpec spec;
    if (!DecodeAccess(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(pc)), spec) ||
        spec.store != isWrite) {
        return 0;
    }

    auto* slot = reinterpret_cast<FastmemSlot*>(reinterpret_cast<uint8_t*>(gprs) -
                                                offsetof(FastmemSlot, x));
    try {
        if (isWrite) {
            if (!EmitStore(*slot, spec.rt, spec.vector, spec.width, guest)) {
                return 0;
            }
            if (spec.pair &&
                !EmitStore(*slot, spec.rt2, spec.vector, spec.width, guest + spec.width)) {
                return 0;
            }
        } else {
            if (!EmitLoad(*slot, spec, spec.rt, guest)) {
                return 0;
            }
            if (spec.pair && !EmitLoad(*slot, spec, spec.rt2, guest + spec.width)) {
                return 0;
            }
        }
    } catch (...) {
        // Memory::Read*/Write* report MMIO accesses that need HLE by throwing; the
        // caller prints far/pc/esr and exits instead of resuming with a stale state.
        return 0;
    }

    if (spec.writeback != Writeback::None) {
        if (spec.rn == 31) {
            return 0;
        }
        slot->x[spec.rn] = spec.writeback == Writeback::Post ? far + spec.offset : far;
    }
    if (isWrite) {
        PerfStats::AddFastmemMmioStore();
    } else {
        PerfStats::AddFastmemMmioLoad();
    }
    return static_cast<uintptr_t>(pc + 4);
}

extern "C" [[noreturn]] void NsmbwFastmemUnhandled(uint64_t far, uint64_t esr, uint64_t pc) {
    uint32_t guest = 0;
    const uint64_t shown = TryGuestAddress(far, guest) ? guest : far;
    const uint32_t insn = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(pc));

    size_t pos = AppendText(0, "[fastmem] unhandled fault far=0x");
    pos = AppendHex(pos, shown, 16);
    pos = AppendText(pos, " pc=0x");
    pos = AppendHex(pos, pc, 16);
    pos = AppendText(pos, " esr=0x");
    pos = AppendHex(pos, esr & 0xFFFFFFFFull, 8);
    pos = AppendText(pos, " insn=0x");
    pos = AppendHex(pos, insn, 8);
    g_faultLine[pos++] = '\n';

    svcOutputDebugString(g_faultLine, pos);
    svcExitProcess();
    for (;;) {
    }
}
