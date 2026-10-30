// Native nw4r::g3d::CalcAnimationFVS<CAnmFmtTraits<ResAnmChrFVS32Data>> (0x802403C0).
// Interpolates one FVS32 keyframe-value channel at `frame`. The translated code
// round-trips every quantized field through guest stack slots and the PSQ GQR
// dispatch; this runs the same arithmetic straight out of guest memory.
//
// NSMBW_G3D_COMPARE=on additionally runs the translated reference, compares the
// two f32 results bit-exactly and returns the translated one. Default (unset or
// off) returns the native result.
#include "hle_stubs.h"
#include "memory.h"
#include "ppc_runtime.h"
#include "runtime_log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// ResAnmChrFVSData / ResAnmChrFVS32Data (pik2wii g3d_resanmchr.h).
constexpr uint32_t kNumFrameValues = 0x00;   // u16
constexpr uint32_t kInvKeyFrameRange = 0x04; // f32
constexpr uint32_t kScale = 0x08;            // f32
constexpr uint32_t kOffset = 0x0C;           // f32
constexpr uint32_t kFrameValues = 0x10;      // ResAnmChrFrm32Data[numFrameValues]
constexpr uint32_t kFrameStride = 4;

// GQR types: 0 float, 4 u8, 5 u16, 6 s8, 7 s16.
constexpr uint32_t kGqrU8 = 4;
constexpr uint32_t kGqrU16 = 5;
constexpr uint32_t kGqrS16 = 7;

// Load parameters live in the upper GQR halfword, store parameters in the lower.
uint32_t LoadType(uint32_t gqr) {
    return (gqr >> 16) & 7u;
}
uint32_t LoadScale(uint32_t gqr) {
    return (gqr >> 24) & 0x3Fu;
}
uint32_t StoreType(uint32_t gqr) {
    return gqr & 7u;
}
uint32_t StoreScale(uint32_t gqr) {
    return (gqr >> 8) & 0x3Fu;
}

double ReadF32(uint32_t addr) {
    return static_cast<double>(MemoryInline::FlatReadFloat32(addr));
}

// ResAnmChrFrm32Data: u8 frame, then 12 bits value and 12 bits s10.5 slope.
uint32_t FrameByte(uint32_t raw) {
    return raw >> 24;
}
uint32_t ValueBits(uint32_t raw) {
    return (raw >> 12) & 0xFFFu;
}
int32_t SlopeS16(uint32_t raw) {
    return static_cast<int32_t>(raw << 20) >> 20;
}

// CResAnmChrFrm<ResAnmChrFrm32Data>::GetFrameF32 / GetValue / GetSlope.
double FrameF32(uint32_t frameByte, uint32_t u8Scale) {
    return static_cast<double>(
        PpcDequantizePsqInline<uint8_t>(static_cast<uint8_t>(frameByte), u8Scale));
}
double Value(uint32_t raw, double scale, double offset, uint32_t u16Scale) {
    const double quantized = static_cast<double>(
        PpcDequantizePsqInline<uint16_t>(static_cast<uint16_t>(ValueBits(raw)), u16Scale));
    return PPC_Fmadds(scale, quantized, offset);
}
double Slope(uint32_t raw, uint32_t s16Scale) {
    return static_cast<double>(
        PpcDequantizePsqInline<int16_t>(static_cast<int16_t>(SlopeS16(raw)), s16Scale));
}

// The translator's body for 0x802403C0. Reached when the GQRs are not the
// encodings the nw4r fast-cast path installs, and used as the reference for
// NSMBW_G3D_COMPARE.
#include "nw4r_g3d_fvs32_translated.inc"

// math::FInv: fres followed by one Newton-Raphson step, as the translated helper.
double Reciprocal(double value) {
    const double estimate = PPC_Fres(PPC_PsFromScalarInline(value));
    const double twiceEstimate = PPC_PsAddInline(estimate, estimate);
    const double estimateSq = PPC_PsMulInline(estimate, estimate);
    return PPC_PsToScalarInline(
        PPC_PsNmsubInline(PPC_PsFromScalarInline(value), estimateSq, twiceEstimate));
}

// gqr2/gqr3/gqr6 are the paired-single quantization registers the guest leaves set
// for u8 frames, u16 key values and s16 slopes.
double CalcAnimationFVS32(uint32_t pFVSData, double frame, uint32_t gqr2, uint32_t gqr3,
                          uint32_t gqr6) {
    const uint32_t u8Load = LoadScale(gqr2);
    const uint32_t u8Store = StoreScale(gqr2);
    const uint32_t u16Load = LoadScale(gqr3);
    const uint32_t u16Store = StoreScale(gqr3);
    const uint32_t s16Load = LoadScale(gqr6);

    const uint32_t numFrameValues = MemoryInline::FlatRead16(pFVSData + kNumFrameValues);
    const double scale = ReadF32(pFVSData + kScale);
    const double offset = ReadF32(pFVSData + kOffset);
    const uint32_t first = pFVSData + kFrameValues;
    const uint32_t last = pFVSData + kFrameValues + (numFrameValues - 1u) * kFrameStride;

    const uint32_t firstRaw = MemoryInline::FlatRead32(first);
    const double firstFrameF32 = FrameF32(FrameByte(firstRaw), u8Load);
    if (frame <= firstFrameF32) {
        return Value(firstRaw, scale, offset, u16Load);
    }

    const uint32_t lastRaw = MemoryInline::FlatRead32(last);
    if (FrameF32(FrameByte(lastRaw), u8Load) <= frame) {
        return Value(lastRaw, scale, offset, u16Load);
    }

    const double numKeyFrame = static_cast<double>(
        PpcDequantizePsqInline<uint16_t>(static_cast<uint16_t>(numFrameValues), u16Load));
    const double frameOffset =
        static_cast<double>(PpcForceSingleValueInline(frame - firstFrameF32));
    const double estimatePos = PpcFmulsInline(ReadF32(pFVSData + kInvKeyFrameRange),
                                              PpcFmulsInline(frameOffset, numKeyFrame));
    const uint32_t estimateIndex =
        PpcScaleAndClampPsqInline<uint16_t>(static_cast<float>(estimatePos), u16Store);
    const uint32_t quantized = PpcScaleAndClampPsqInline<uint8_t>(static_cast<float>(frame), u8Store);

    uint32_t left = pFVSData + kFrameValues + estimateIndex * kFrameStride;
    uint32_t leftFrame = MemoryInline::FlatRead8(left);
    if (quantized < leftFrame) {
        do {
            left -= kFrameStride;
            leftFrame = MemoryInline::FlatRead8(left);
        } while (quantized < leftFrame);
    } else {
        do {
            left += kFrameStride;
            leftFrame = MemoryInline::FlatRead8(left);
        } while (leftFrame <= quantized);
        left -= kFrameStride;
    }

    // The translated code reloads the frame from `left` after the walk, not the
    // byte the loop stopped on.
    const double leftFrameF32 = FrameF32(MemoryInline::FlatRead8(left), u8Load);
    const uint32_t leftRaw = MemoryInline::FlatRead32(left);
    if (frame == leftFrameF32) {
        return Value(leftRaw, scale, offset, u16Load);
    }

    const uint32_t rightRaw = MemoryInline::FlatRead32(left + kFrameStride);
    const double v0 = Value(leftRaw, scale, offset, u16Load);
    const double t0 = Slope(leftRaw, s16Load);
    const double v1 = Value(rightRaw, scale, offset, u16Load);
    const double t1 = Slope(rightRaw, s16Load);

    const double v0MinusV1 = static_cast<double>(PpcForceSingleValueInline(v0 - v1));
    const double frameDelta = static_cast<double>(PpcForceSingleValueInline(frame - leftFrameF32));
    const double keyFrameDelta = static_cast<double>(PpcForceSingleValueInline(
        FrameF32(FrameByte(rightRaw), u8Load) - leftFrameF32));
    const double keyFrameDeltaInv = Reciprocal(keyFrameDelta);

    // HermiteInterpolation(v0, t0, v1, t1, s = frameDelta * inv, frameDelta).
    const double s = PpcFmulsInline(frameDelta, keyFrameDeltaInv);
    const double sMinus1 = PPC_Fmsubs(frameDelta, keyFrameDeltaInv, 1.0);
    const double twoSMinus3 = PPC_Fmsubs(2.0, s, 3.0);
    const double sTimesT1 = PpcFmulsInline(s, t1);
    const double deltaTimesS1 = PpcFmulsInline(frameDelta, sMinus1);
    const double tangents = PPC_Fmadds(sMinus1, t0, sTimesT1);
    double result = PpcFmulsInline(twoSMinus3, v0MinusV1);
    result = PpcFmulsInline(s, result);
    result = PPC_Fmadds(s, result, v0);
    return PPC_Fmadds(deltaTimesS1, tangents, result);
}

bool CompareEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_G3D_COMPARE");
        if (value == nullptr) {
            return false;
        }
        return std::strcmp(value, "on") == 0 || std::strcmp(value, "1") == 0 ||
               std::strcmp(value, "compare") == 0;
    }();
    return enabled;
}

bool GqrFastPath(uint32_t gqr2, uint32_t gqr3, uint32_t gqr6) {
    return LoadType(gqr2) == kGqrU8 && StoreType(gqr2) == kGqrU8 &&
           LoadType(gqr3) == kGqrU16 && StoreType(gqr3) == kGqrU16 &&
           LoadType(gqr6) == kGqrS16;
}

}  // namespace

extern "C" void NW4R_G3D_CalcAnimationFVS32_HLE(CpuContext* ctx) {
    const uint32_t pFVSData = ctx->gpr[3];
    const double frame = ctx->fpr[1].d;
    const uint32_t gqr2 = ctx->gqr[2];
    const uint32_t gqr3 = ctx->gqr[3];
    const uint32_t gqr6 = ctx->gqr[6];

    if (!GqrFastPath(gqr2, gqr3, gqr6)) {
        CalcAnimationFVS32Translated(ctx);
        return;
    }
    if (!CompareEnabled()) {
        ctx->fpr[1].d = CalcAnimationFVS32(pFVSData, frame, gqr2, gqr3, gqr6);
        return;
    }

    // Compare mode: run the translated reference on the caller's registers, then
    // rewind them and run the native body on the same inputs. The translated
    // result is the one left behind.
    uint32_t gpr[8];
    PPC_FPR fpr[12];
    for (uint32_t i = 0; i < 8; ++i) {
        gpr[i] = ctx->gpr[i];
    }
    for (uint32_t i = 0; i < 12; ++i) {
        fpr[i] = ctx->fpr[i];
    }
    const uint32_t cr = ctx->cr;
    const uint32_t xer = ctx->xer;

    CalcAnimationFVS32Translated(ctx);
    const uint32_t translatedBits = PpcBitCastToU32Inline(static_cast<float>(ctx->fpr[1].d));

    for (uint32_t i = 0; i < 8; ++i) {
        ctx->gpr[i] = gpr[i];
    }
    for (uint32_t i = 0; i < 12; ++i) {
        ctx->fpr[i] = fpr[i];
    }
    ctx->cr = cr;
    ctx->xer = xer;

    const uint32_t nativeBits = PpcBitCastToU32Inline(
        static_cast<float>(CalcAnimationFVS32(pFVSData, frame, gqr2, gqr3, gqr6)));

    static uint32_t compares = 0;
    static uint32_t mismatches = 0;
    ++compares;
    if (translatedBits != nativeBits) {
        if (mismatches++ == 0) {
            RT_LOGF(RT_TAG_HLE,
                    "g3d fvs32 compare mismatch: fvs %08X frame %.9g translated %08X native %08X\n",
                    pFVSData, frame, translatedBits, nativeBits);
        }
    }
    if (compares == 1) {
        RT_LOGF(RT_TAG_HLE, "g3d fvs32 compare: first call fvs %08X frame %.9g\n", pFVSData, frame);
    }
    if (compares % 100000u == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d fvs32 compare: %u calls, %u mismatches\n", compares, mismatches);
    }

    ctx->fpr[1].d = static_cast<double>(PpcBitCastToFloatInline(translatedBits));
}

PPC_NATIVE_OVERRIDE_VOID(802403C0, NW4R_G3D_CalcAnimationFVS32_HLE, (CpuContext* ctx), (ctx));
