// Native nw4r::g3d math::SinFIdx (0x80237BD0) and math::CosFIdx (0x80237C40).
//
// Both reduce |fidx| modulo the SDA period (65536.0f), split it into the quantized table index and
// the residual through the GQR3 paired-single convert pair, interpolate the table entry (sin at
// +0/+8, cos at +4/+12) and restore the input's sign. The sequence below mirrors the translated
// bodies step for step, including the single-precision rounding of the subtraction and the final
// sum. The translated copies are kept for the verify mode and for NSMBW_SINFIDX=0.

#include "abi_bridge.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "runtime_log.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" void SinFIdxTranslated(CpuContext* ctx);
extern "C" void CosFIdxTranslated(CpuContext* ctx);

namespace {

constexpr uint32_t kSinCosTable = 0x802F74A0u;
constexpr uint32_t kPeriodSdaOffset = 19424u;  // r2-relative 65536.0f
constexpr uint32_t kScratchBytes = 32u;

bool NativeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_SINFIDX");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

bool VerifyEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_SINFIDX_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

void Interpolate(CpuContext* ctx, bool cosine) {
    const double input = ctx->fpr[1].d;
    const double period = MemoryInline::FlatReadFloat32(ctx->gpr[2] - kPeriodSdaOffset);
    double magnitude = std::fabs(input);
    while (magnitude > period) {
        magnitude = PpcForceSingleValueInline(magnitude - period);
    }

    const uint32_t savedSp = ctx->gpr[1];
    const uint32_t scratch = savedSp - kScratchBytes;
    ctx->gpr[1] = scratch;

    PPC_PsqStStackInline<1u, 3u>(ctx, scratch + 12u, PPC_PsFromScalarInline(magnitude));
    const uint16_t whole = MemoryInline::FlatRead16(scratch + 12u);
    MemoryInline::FlatWriteRam16(scratch + 8u, whole);

    PPC_FPR loaded{};
    PpcSetPairedFprInline(loaded, PPC_PsqLStackInline<1u, 3u>(ctx, scratch + 8u));
    const double residual = PpcForceSingleValueInline(magnitude - PPC_PsToScalarInline(loaded.d));

    const uint32_t index = PpcRotl32Inline(static_cast<uint32_t>(whole), 4u) & 4080u;
    const uint32_t entry = kSinCosTable + index;
    const double base = MemoryInline::FlatReadFloat32(entry + (cosine ? 4u : 0u));
    const double delta = MemoryInline::FlatReadFloat32(entry + (cosine ? 12u : 8u));
    const float value = PpcForceSingleValueInline(base + PpcFmulsInline(residual, delta));

    ctx->gpr[1] = savedSp;
    // SinFIdx restores the input's sign; CosFIdx is even and has no sign fix in the guest body.
    ctx->fpr[1].d = (!cosine && input < 0.0) ? -static_cast<double>(value)
                                             : static_cast<double>(value);
}

uint64_t g_calls = 0;
uint64_t g_mismatches = 0;

void RunVerified(CpuContext* ctx, bool cosine) {
    const CpuContext before = *ctx;
    if (cosine) {
        CosFIdxTranslated(ctx);
    } else {
        SinFIdxTranslated(ctx);
    }
    const CpuContext afterTranslated = *ctx;
    const double expected = afterTranslated.fpr[1].d;

    *ctx = before;
    Interpolate(ctx, cosine);
    const double result = ctx->fpr[1].d;

    if (std::memcmp(&result, &expected, sizeof(result)) != 0) {
        ++g_mismatches;
        if (g_mismatches <= 5) {
            RT_LOGF(RT_TAG_RUNTIME, "sinfidx verify: %s in %016llx expected %016llx got %016llx\n",
                    cosine ? "cos" : "sin", static_cast<unsigned long long>(before.fpr[1].raw),
                    static_cast<unsigned long long>(afterTranslated.fpr[1].raw),
                    static_cast<unsigned long long>(ctx->fpr[1].raw));
        }
    }
    // Verify mode keeps the translated result so a mismatch cannot change the game.
    *ctx = afterTranslated;
    if ((++g_calls % 200000) == 0) {
        RT_LOGF(RT_TAG_RUNTIME, "sinfidx verify: %llu calls, %llu mismatches\n",
                static_cast<unsigned long long>(g_calls),
                static_cast<unsigned long long>(g_mismatches));
    }
}

} // namespace

extern "C" void NW4R_G3D_SinFIdx_HLE(CpuContext* ctx) {
    if (!NativeEnabled()) {
        SinFIdxTranslated(ctx);
        return;
    }
    if (VerifyEnabled()) {
        RunVerified(ctx, false);
        return;
    }
    Interpolate(ctx, false);
}

extern "C" void NW4R_G3D_CosFIdx_HLE(CpuContext* ctx) {
    if (!NativeEnabled()) {
        CosFIdxTranslated(ctx);
        return;
    }
    if (VerifyEnabled()) {
        RunVerified(ctx, true);
        return;
    }
    Interpolate(ctx, true);
}

#include "nw4r_g3d_sincos_translated.inc"

PPC_NATIVE_OVERRIDE_VOID(80237bd0, NW4R_G3D_SinFIdx_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80237c40, NW4R_G3D_CosFIdx_HLE, (CpuContext* ctx), (ctx));
