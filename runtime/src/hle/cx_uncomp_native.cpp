// Native CXReadUncompLZ (0x801D6B60), the Wii SDK's chunked LZ77 decoder.
//
// The streaming file reader feeds compressed chunks through this function, which continues the
// decode from the context it keeps (destination cursor, remaining bytes, flag bit and back-reference
// state). It is pure memory work, so it runs as a guard-free leaf. The sequence mirrors
// CXStreamingUncompression.c from wii-ipl line for line, including the header parse and the error
// returns. NSMBW_CX_VERIFY=1 runs the translated body on the same input first and compares the
// return value, the context and the bytes written.

#include "abi_bridge.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "runtime_log.h"

#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <vector>

extern "C" void CXReadUncompLZTranslated(CpuContext* ctx);

namespace {

constexpr uint32_t kDst = 0x00;
constexpr uint32_t kRemaining = 0x04;
constexpr uint32_t kTotal = 0x08;
constexpr uint32_t kCopyLen = 0x0C;
constexpr uint32_t kState = 0x10;
constexpr uint32_t kFlags = 0x11;
constexpr uint32_t kBits = 0x12;
constexpr uint32_t kHdrLen = 0x13;
constexpr uint32_t kSub = 0x14;
constexpr uint32_t kContextBytes = 0x18;

uint8_t R8(uint32_t address) {
    return MemoryInline::FlatRead8(address);
}

uint32_t R32(uint32_t address) {
    return MemoryInline::FlatRead32(address);
}

void W8(uint32_t address, uint8_t value) {
    MemoryInline::FlatWrite8(address, value);
}

void W32(uint32_t address, uint32_t value) {
    MemoryInline::FlatWrite32(address, value);
}

int32_t ReadHeader(uint8_t& hdrLen, int32_t& outLen, uint32_t src, int32_t srcSize, int32_t total) {
    int32_t parsed = 0;
    while (hdrLen) {
        --hdrLen;
        const uint8_t byte = R8(src);
        if (hdrLen <= 3) {
            outLen |= static_cast<int32_t>(byte) << ((3 - hdrLen) * 8);
        } else if (hdrLen <= 6) {
            outLen |= static_cast<int32_t>(byte) << ((6 - hdrLen) * 8);
        }
        ++src;
        ++parsed;
        if (hdrLen == 4 && outLen > 0) {
            hdrLen = 0;
        }
        --srcSize;
        if (srcSize == 0 && hdrLen != 0) {
            return parsed;
        }
    }
    if (total > 0 && total < outLen) {
        outLen = total;
    }
    return parsed;
}

int32_t Impl(uint32_t ctx, uint32_t src, uint32_t size) {
    uint8_t hdrLen = R8(ctx + kHdrLen);
    if (hdrLen) {
        if (hdrLen == 8) {
            const uint8_t byte = R8(src);
            if ((byte & 0xF0u) != 0x10u) {
                return -1;
            }
            const uint8_t sub = static_cast<uint8_t>(byte & 0x0Fu);
            W8(ctx + kSub, sub);
            if (sub != 0 && sub != 1) {
                return -1;
            }
        }
        int32_t outLen = static_cast<int32_t>(R32(ctx + kRemaining));
        const int32_t parsed =
            ReadHeader(hdrLen, outLen, src, static_cast<int32_t>(size), static_cast<int32_t>(R32(ctx + kTotal)));
        src += static_cast<uint32_t>(parsed);
        size -= static_cast<uint32_t>(parsed);
        W8(ctx + kHdrLen, hdrLen);
        W32(ctx + kRemaining, static_cast<uint32_t>(outLen));
        if (size == 0) {
            return hdrLen ? -1 : outLen;
        }
    }

    uint32_t dst = R32(ctx + kDst);
    int32_t remaining = static_cast<int32_t>(R32(ctx + kRemaining));
    int32_t copyLen = static_cast<int32_t>(R32(ctx + kCopyLen));
    uint8_t state = R8(ctx + kState);
    uint8_t flags = R8(ctx + kFlags);
    uint8_t bits = R8(ctx + kBits);
    const uint8_t sub = R8(ctx + kSub);

    auto store = [&]() {
        W32(ctx + kDst, dst);
        W32(ctx + kRemaining, static_cast<uint32_t>(remaining));
        W32(ctx + kCopyLen, static_cast<uint32_t>(copyLen));
        W8(ctx + kState, state);
        W8(ctx + kFlags, flags);
        W8(ctx + kBits, bits);
    };

    bool done = false;
    while (remaining > 0 && !done) {
        while (bits) {
            if (size == 0) {
                store();
                return remaining;
            }
            if ((flags & 0x80u) == 0) {
                W8(dst, R8(src));
                ++dst;
                ++src;
                --remaining;
                --size;
            } else {
                while (state) {
                    --state;
                    if (sub == 0) {
                        copyLen = static_cast<int32_t>(R8(src)) + 0x30;
                        ++src;
                        state = 0;
                    } else if (state == 2) {
                        copyLen = R8(src);
                        ++src;
                        if ((copyLen >> 4) == 1) {
                            copyLen = (copyLen & 0x0F) << 16;
                            copyLen += 0x1110;
                        } else if ((copyLen >> 4) == 0) {
                            copyLen = (copyLen & 0x0F) << 8;
                            copyLen += 0x110;
                            state = 1;
                        } else {
                            copyLen += 0x10;
                            state = 0;
                        }
                    } else if (state == 1) {
                        copyLen += static_cast<int32_t>(R8(src)) << 8;
                        ++src;
                    } else {
                        copyLen += R8(src);
                        ++src;
                    }
                    --size;
                    if (size == 0) {
                        store();
                        return remaining;
                    }
                }
                int32_t back = (copyLen & 0x0F) << 8;
                copyLen >>= 4;
                back = static_cast<int32_t>(static_cast<uint32_t>(back) | R8(src)) + 1;
                ++src;
                --size;
                state = 3;
                if (copyLen > remaining) {
                    if (R32(ctx + kTotal) == 0) {
                        store();
                        return -4;
                    }
                    copyLen = remaining;
                }
                while (copyLen > 0) {
                    W8(dst, R8(dst - static_cast<uint32_t>(back)));
                    ++dst;
                    --remaining;
                    --copyLen;
                }
            }
            if (remaining == 0) {
                done = true;
                break;
            }
            flags = static_cast<uint8_t>(flags << 1);
            --bits;
        }
        if (!done) {
            if (size == 0) {
                store();
                return remaining;
            }
            flags = R8(src);
            ++src;
            bits = 8;
            --size;
        }
    }

    store();
    if (R32(ctx + kTotal) == 0 && size > 32) {
        return -3;
    }
    return 0;
}

bool VerifyEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_CX_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

uint64_t g_calls = 0;
uint64_t g_mismatches = 0;

int32_t RunVerified(uint32_t ctx, uint32_t src, uint32_t size) {
    CpuContext* cpu = TryGetCpuContext();
    if (cpu == nullptr) {
        return Impl(ctx, src, size);
    }

    uint8_t contextBefore[kContextBytes];
    for (uint32_t i = 0; i < kContextBytes; ++i) {
        contextBefore[i] = R8(ctx + i);
    }
    const uint32_t dstBefore = R32(ctx + kDst);
    const int32_t remainingBefore = static_cast<int32_t>(R32(ctx + kRemaining));
    std::vector<uint8_t> outputBefore(remainingBefore > 0 ? static_cast<size_t>(remainingBefore) : 0);
    for (size_t i = 0; i < outputBefore.size(); ++i) {
        outputBefore[i] = R8(dstBefore + static_cast<uint32_t>(i));
    }

    const uint32_t savedGpr3 = cpu->gpr[3];
    const uint32_t savedGpr4 = cpu->gpr[4];
    const uint32_t savedGpr5 = cpu->gpr[5];
    cpu->gpr[3] = ctx;
    cpu->gpr[4] = src;
    cpu->gpr[5] = size;
    const auto translatedStart = std::chrono::steady_clock::now();
    CXReadUncompLZTranslated(cpu);
    const auto translatedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - translatedStart).count();
    const int32_t expected = static_cast<int32_t>(cpu->gpr[3]);
    cpu->gpr[3] = savedGpr3;
    cpu->gpr[4] = savedGpr4;
    cpu->gpr[5] = savedGpr5;

    uint8_t contextTranslated[kContextBytes];
    for (uint32_t i = 0; i < kContextBytes; ++i) {
        contextTranslated[i] = R8(ctx + i);
    }
    std::vector<uint8_t> outputTranslated(outputBefore.size());
    for (size_t i = 0; i < outputTranslated.size(); ++i) {
        outputTranslated[i] = R8(dstBefore + static_cast<uint32_t>(i));
    }

    // Restore the input state, then run the native on the same call.
    for (uint32_t i = 0; i < kContextBytes; ++i) {
        W8(ctx + i, contextBefore[i]);
    }
    for (size_t i = 0; i < outputBefore.size(); ++i) {
        W8(dstBefore + static_cast<uint32_t>(i), outputBefore[i]);
    }
    const auto nativeStart = std::chrono::steady_clock::now();
    const int32_t result = Impl(ctx, src, size);
    const auto nativeMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - nativeStart).count();

    bool mismatch = result != expected;
    if (!mismatch) {
        for (uint32_t i = 0; i < kContextBytes && !mismatch; ++i) {
            mismatch = R8(ctx + i) != contextTranslated[i];
        }
    }
    for (size_t i = 0; i < outputTranslated.size() && !mismatch; ++i) {
        mismatch = R8(dstBefore + static_cast<uint32_t>(i)) != outputTranslated[i];
    }
    ++g_calls;
    if (mismatch) {
        ++g_mismatches;
        if (g_mismatches <= 5) {
            RT_LOGF(RT_TAG_RUNTIME,
                    "cx lz verify: ctx %08X src %08X size %u expected %d got %d "
                    "(translated %lld us, native %lld us)\n", ctx, src, size, expected, result,
                    static_cast<long long>(translatedMicros), static_cast<long long>(nativeMicros));
        }
    }
    RT_LOGF(RT_TAG_RUNTIME, "cx lz verify: %llu calls, %llu mismatches (translated %lld us, native %lld us)\n",
            static_cast<unsigned long long>(g_calls),
            static_cast<unsigned long long>(g_mismatches),
            static_cast<long long>(translatedMicros), static_cast<long long>(nativeMicros));

    // Verify mode keeps the translated outcome so a mismatch cannot change the game.
    for (uint32_t i = 0; i < kContextBytes; ++i) {
        W8(ctx + i, contextTranslated[i]);
    }
    for (size_t i = 0; i < outputTranslated.size(); ++i) {
        W8(dstBefore + static_cast<uint32_t>(i), outputTranslated[i]);
    }
    return expected;
}

} // namespace

extern "C" int32_t CXReadUncompLZ_Native(uint32_t ctx, uint32_t src, uint32_t size) {
    if (VerifyEnabled()) {
        return RunVerified(ctx, src, size);
    }
    return Impl(ctx, src, size);
}

#include "cx_uncomp_translated.inc"

PPC_NATIVE_OVERRIDE(801D6B60, CXReadUncompLZ_Native, int32_t,
                    (uint32_t ctx, uint32_t src, uint32_t size), (ctx, src, size));
