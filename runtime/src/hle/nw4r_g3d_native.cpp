// Native nw4r::g3d overrides that keep guest memory access through MemoryInline.
//
// 0x802403C0 CalcAnimationFVS<CAnmFmtTraits<ResAnmChrFVS32Data>>
//   Interpolates one FVS32 keyframe-value channel at `frame`. The translated code
//   round-trips every quantized field through guest stack slots and the PSQ GQR
//   dispatch; this runs the same arithmetic straight out of guest memory.
//
// 0x80248330 AnmObjChrBlend::GetResult
//   Blends the ChrAnmResult of every child that can provide `id` into one
//   ChrAnmResult, including the matrix/quaternion fixup.
//
// NSMBW_G3D_COMPARE=on additionally runs the translated reference, compares the
// results bit-exactly and returns the translated one. Default (unset or off)
// returns the native result.
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

namespace {

// AnmObjChrBlend / AnmObjChrRes (pik2wii include/nw4r/g3d/g3d_anmchr.h). The
// offsets are the ones the guest body uses.
constexpr uint32_t kBlendFlags = 0x0C;     // u32 mFlags
constexpr uint32_t kBlendNumChild = 0x18;  // u32 mChildrenArraySize
constexpr uint32_t kBlendChildren = 0x1C;  // AnmObjChrRes*[] mpChildrenArray
constexpr uint32_t kBlendWeights = 0x20;   // f32[] mpWeightArray
constexpr uint32_t kChildBinding = 0x14;   // AnmObjChrRes::mpBinding (u16* base)
constexpr uint32_t kVtableGetResult = 0x38;

// AnmObjChr::TestExistence skips BINDING_INVALID | BINDING_UNDEFINED.
constexpr uint32_t kBindingSkipMask = 0xC000;

// AnmObjChr animation flags.
constexpr uint32_t kFlagUseQuatBlend = 0x08;
constexpr uint32_t kFlagUseAccScale = 0x10;

// ChrAnmResult.
constexpr uint32_t kResultFlags = 0x00;
constexpr uint32_t kResultScale = 0x04;  // Vec s
constexpr uint32_t kResultMtx = 0x1C;    // math::MTX34 rt
constexpr uint32_t kResultDwords = 0x4C / 4;  // flags + s + rawR + rt

// ChrAnmResult::flags bits.
constexpr uint32_t kChrFlagScaleOne = 0x08;
constexpr uint32_t kChrFlagRotZero = 0x20;
constexpr uint32_t kChrFlagTransZero = 0x40;
constexpr uint32_t kChrFlagRotRawFmt = 0x80000000u;

// Guest scratch frame. The slots match the translated body exactly so the child
// calls see the same stack layout: Vec t, QUAT q, QUAT rot, MTX33 firstRot and
// the ChrAnmResult the children write through pResult. Children leave parts of
// that buffer untouched, so it has to be the same guest address the translated
// body would use.
constexpr uint32_t kFrameSize = 352;
constexpr uint32_t kFrameVecT = 0x08;
constexpr uint32_t kFrameQuat = 0x18;
constexpr uint32_t kFrameRot = 0x28;
constexpr uint32_t kFrameFirstRot = 0x38;
constexpr uint32_t kFrameChildResult = 0x5C;

// ChrAnmResult::rt element offsets (rows are 16 bytes apart), named after the
// guest's access pattern so the transcription stays auditable.
constexpr uint32_t kMtx00 = 0;
constexpr uint32_t kMtx01 = 4;
constexpr uint32_t kMtx02 = 8;
constexpr uint32_t kMtx03 = 12;
constexpr uint32_t kMtx10 = 16;
constexpr uint32_t kMtx11 = 20;
constexpr uint32_t kMtx12 = 24;
constexpr uint32_t kMtx13 = 28;
constexpr uint32_t kMtx20 = 32;
constexpr uint32_t kMtx23 = 44;

// Only the guest stack frame is written here, so the stack PSQ helpers are used
// where the translated body uses them and the GQR forms where it uses those.
void StorePairStack(CpuContext* ctx, uint32_t addr, double pair) {
    PPC_PsqStStackInline<0u, 0u>(ctx, addr, pair);
}

// math::MTX33Identity (0x802380A0), as inlined into the guest body.
void Mtx33Identity(uint32_t sda, CpuContext* ctx, uint32_t dst) {
    const double zero = ReadF32(sda - 19384);
    const double one = ReadF32(sda - 19380);
    const double merged =
        PPC_PsMerge00Inline(PPC_PsFromScalarInline(one), PPC_PsFromScalarInline(zero));
    StorePairStack(ctx, dst + 8, PPC_PsFromScalarInline(zero));
    StorePairStack(ctx, dst + 24, PPC_PsFromScalarInline(zero));
    StorePairStack(ctx, dst + 0, merged);
    StorePairStack(ctx, dst + 16, merged);
    MemoryInline::FlatWriteFloat32(dst + 32, one);
}

// math::VEC3Cross (0x801C1530) with the guest's (r3 = a, r4 = b, r5 = out) call
// pattern, as inlined into the guest body.
void Vec3Cross(CpuContext* ctx, uint32_t gqr0, uint32_t a, uint32_t b, uint32_t out) {
    double f1 = PPC_PsqLGqrInline<0u, 0u>(ctx, gqr0, b);
    const double f2 = ReadF32(a + 8);
    const double f0 = PPC_PsqLGqrInline<0u, 0u>(ctx, gqr0, a);
    const double f6 = PPC_PsMerge10Inline(f1, f1);
    const double f3 = ReadF32(b + 8);
    const double f4 = PPC_PsMulInline(f1, PPC_PsFromScalarInline(f2));
    const double f7 = PPC_PsMuls0Inline(f1, f0);
    const double f5 = PPC_PsMsubInline(f0, PPC_PsFromScalarInline(f3), f4);
    const double f8 = PPC_PsMsubInline(f0, f6, f7);
    const double f9 = PPC_PsMerge11Inline(f5, f5);
    double f10 = PPC_PsMerge01Inline(f5, f8);
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, out, f9);
    f10 = PPC_PsNegInline(f10);
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, out + 4, f10);
}

// math::VEC3Normalize (0x801C1470), as inlined into the guest body.
void Vec3Normalize(uint32_t sda, CpuContext* ctx, uint32_t gqr0, uint32_t src, uint32_t dst) {
    double f2 = PPC_PsqLGqrInline<0u, 0u>(ctx, gqr0, src);
    double f3 = PPC_PsqLGqrInline<1u, 0u>(ctx, gqr0, src + 8);
    double f5 = PPC_PsMulInline(f2, f2);
    double f0 = ReadF32(sda - 20248);
    const double f1 = ReadF32(sda - 20244);
    double f4 = PPC_PsMaddInline(f3, f3, f5);
    f4 = PPC_PsSum0Inline(f4, f5, f3);
    f5 = PPC_Frsqrte(PPC_PsToScalarInline(f4));
    double f6 = PpcFmulsInline(f5, f5);
    f0 = PpcFmulsInline(f5, f0);
    f6 = PPC_Fnmsubs(f6, PPC_PsToScalarInline(f4), f1);
    f5 = PpcFmulsInline(f6, f0);
    f2 = PPC_PsMuls0Inline(f2, PPC_PsFromScalarInline(f5));
    f3 = PPC_PsMuls0Inline(f3, PPC_PsFromScalarInline(f5));
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, dst, f2);
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, dst + 8, f3);
}

// math::QUATToMTX34 (0x801C0CF0), as inlined into the guest body. The locals
// reuse the guest's registers in the same order.
void QuatToMtx34(uint32_t sda, CpuContext* ctx, uint32_t gqr0, uint32_t dst, uint32_t quat) {
    double f4 = PPC_PsqLStackInline<0u, 0u>(ctx, quat);
    double f5 = PPC_PsqLStackInline<0u, 0u>(ctx, quat + 8);
    double f6 = PPC_PsMulInline(f4, f4);
    const double f1 = ReadF32(sda - 20304);
    double f9 = PPC_PsMerge10Inline(f4, f4);
    const double f0 = static_cast<double>(PpcForceSingleValueInline(f1 - f1));
    double f8 = PPC_PsMaddInline(f5, f5, f6);
    double f10 = PPC_PsMuls1Inline(f5, f5);
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, dst + 12, PPC_PsFromScalarInline(f0));
    const double f2 = static_cast<double>(PpcForceSingleValueInline(f1 + f1));
    double f3 = PPC_PsSum0Inline(f8, f8, f8);
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, dst + 44, PPC_PsFromScalarInline(f0));
    double f7 = PPC_PsMulInline(f5, f5);
    double f12 = PPC_PsMaddInline(f4, f9, f10);
    double f13 = PPC_Fres(f3);
    f3 = PPC_PsNmsubInline(f3, f13, PPC_PsFromScalarInline(f2));
    double f11 = PPC_PsMuls1Inline(f9, f5);
    f10 = PPC_PsMsubInline(f4, f9, f10);
    f3 = PPC_PsMulInline(f13, f3);
    f9 = PPC_PsMadds0Inline(f4, f5, f11);
    f8 = PPC_PsSum1Inline(f7, f6, f8);
    const double f3Scalar = PpcFmulsInline(PPC_PsToScalarInline(f3), f2);
    f11 = PPC_PsNmsubInline(f11, PPC_PsFromScalarInline(f2), f9);
    f6 = PPC_PsSum0Inline(f6, f6, f6);
    f9 = PPC_PsMulInline(f9, PPC_PsFromScalarInline(f3Scalar));
    f11 = PPC_PsMulInline(f11, PPC_PsFromScalarInline(f3Scalar));
    f8 = PPC_PsNmsubInline(f8, PPC_PsFromScalarInline(f3Scalar), PPC_PsFromScalarInline(f1));
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, dst + 8, f9);
    f12 = PPC_PsMulInline(f12, PPC_PsFromScalarInline(f3Scalar));
    f10 = PPC_PsMulInline(f10, PPC_PsFromScalarInline(f3Scalar));
    f7 = PPC_PsMerge10Inline(f11, PPC_PsFromScalarInline(f0));
    f5 = PPC_PsMerge00Inline(f12, f8);
    f4 = PPC_PsMerge10Inline(f8, f10);
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, dst + 24, f7);
    f13 = PPC_PsMerge01Inline(f11, f9);
    f6 = PPC_PsNmsubInline(f6, PPC_PsFromScalarInline(f3Scalar), PPC_PsFromScalarInline(f1));
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, dst + 16, f5);
    PPC_PsqStGqrInline<1u, 0u>(ctx, gqr0, dst + 40, f6);
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, dst + 0, f4);
    PPC_PsqStGqrInline<0u, 0u>(ctx, gqr0, dst + 32, f13);
}

// Squared length of a VEC3, the way the guest body tests it.
double VecLengthSquared(CpuContext* ctx, uint32_t gqr0, uint32_t vec) {
    double f2 = PPC_PsqLGqrInline<0u, 0u>(ctx, gqr0, vec);
    double f1 = ReadF32(vec + 8);
    f2 = PPC_PsMulInline(f2, f2);
    f1 = PPC_PsMaddInline(PPC_PsFromScalarInline(f1), PPC_PsFromScalarInline(f1), f2);
    f1 = PPC_PsSum0Inline(f1, f2, f2);
    return PPC_PsToScalarInline(f1);
}

void InvokeChildGetResult(CpuContext* ctx, uint32_t child, uint32_t pResult, uint32_t id) {
    const uint32_t entry = MemoryInline::FlatRead32(MemoryInline::FlatRead32(child) + kVtableGetResult);
    ctx->gpr[3] = child;
    ctx->gpr[4] = pResult;
    ctx->gpr[5] = id;
    ctx->gpr[12] = entry;
    ctx->ctr = entry;
    ctx->lr = 0x80248518u;
    InvokeIndirectCpu(entry, ctx);
}

// math::FLog (0x80237B00) / math::FExp (0x80237A60) keep the guest's own
// implementation; both run on the guest stack of the caller.
double GuestFLog(CpuContext* ctx, double value) {
    ctx->fpr[1].d = value;
    InvokeDirectCpu<0x80237B00u>(ctx);
    return ctx->fpr[1].d;
}

double GuestFExp(CpuContext* ctx, double value) {
    ctx->fpr[1].d = value;
    InvokeDirectCpu<0x80237A60u>(ctx);
    return ctx->fpr[1].d;
}

// The translated body for 0x80248330, kept as the NSMBW_G3D_COMPARE reference.
#include "nw4r_g3d_chrblend_translated.inc"

void AnmObjChrBlendGetResultNative(CpuContext* ctx) {
    const uint32_t savedR1 = ctx->gpr[1];
    const uint32_t savedLr = ctx->lr;
    const uint32_t sda = ctx->gpr[2];
    const uint32_t self = ctx->gpr[3];
    const uint32_t pResult = ctx->gpr[4];
    const uint32_t id = ctx->gpr[5];
    const uint32_t gqr0 = ctx->gqr[0];
    const uint32_t frame = savedR1 - kFrameSize;
    const uint32_t bindingOffset = id * 2u;

    // sdata2 constants, read exactly where the guest body reads them.
    const double zero = ReadF32(sda - 19008);
    const double one = ReadF32(sda - 19004);
    const double logZero = ReadF32(sda - 19000);

    MemoryInline::FlatWrite32(frame, savedR1);
    ctx->gpr[1] = frame;

    // First pass: total weight plus the first child that can provide `id`.
    const int32_t childCount =
        static_cast<int32_t>(MemoryInline::FlatRead32(self + kBlendNumChild));
    const uint32_t children = MemoryInline::FlatRead32(self + kBlendChildren);
    const uint32_t weights = MemoryInline::FlatRead32(self + kBlendWeights);
    uint32_t firstChild = 0;
    uint32_t blendNum = 0;
    double weightSum = zero;
    for (int32_t i = 0; i < childCount; ++i) {
        const uint32_t slot = static_cast<uint32_t>(i) * 4u;
        const uint32_t child = MemoryInline::FlatRead32(children + slot);
        const double weight = ReadF32(weights + slot);
        if (child == 0 || weight == zero) {
            continue;
        }
        const uint32_t binding =
            MemoryInline::FlatRead32(child + kChildBinding) + bindingOffset;
        if ((MemoryInline::FlatRead16(binding) & kBindingSkipMask) != 0) {
            continue;
        }
        weightSum = static_cast<double>(PpcForceSingleValueInline(weightSum + weight));
        ++blendNum;
        if (firstChild == 0) {
            firstChild = child;
        }
    }

    uint32_t result = pResult;
    if (blendNum == 0) {
        MemoryInline::FlatWrite32(pResult + kResultFlags, 0);
    } else if (blendNum == 1) {
        InvokeChildGetResult(ctx, firstChild, pResult, id);
        result = ctx->gpr[3];
    } else {
        const uint32_t anmFlags = MemoryInline::FlatRead32(self + kBlendFlags);
        const bool useQuat = (anmFlags & kFlagUseQuatBlend) != 0;
        const bool useAccScale = (anmFlags & kFlagUseAccScale) != 0;

        // invWeightSum = math::FInv(weightSum): fres plus one Newton step.
        const double estimate = PPC_Fres(PPC_PsFromScalarInline(weightSum));
        const double twiceEstimate = PPC_PsAddInline(estimate, estimate);
        const double estimateSq = PPC_PsMulInline(estimate, estimate);
        const double invWeightSum =
            PPC_PsNmsubInline(PPC_PsFromScalarInline(weightSum), estimateSq, twiceEstimate);

        // rot = QUAT(0, 0, 0, 1).
        MemoryInline::FlatWriteFloat32(frame + kFrameRot + 0, zero);
        MemoryInline::FlatWriteFloat32(frame + kFrameRot + 4, zero);
        MemoryInline::FlatWriteFloat32(frame + kFrameRot + 8, zero);
        MemoryInline::FlatWriteFloat32(frame + kFrameRot + 12, one);

        double esx = zero;
        double esy = zero;
        double esz = zero;
        double addedWeight = zero;

        MemoryInline::FlatWrite32(pResult + kResultFlags, 0xFFFFFFFFu);
        MemoryInline::FlatWriteFloat32(pResult + kResultScale + 0, zero);
        MemoryInline::FlatWriteFloat32(pResult + kResultScale + 4, zero);
        MemoryInline::FlatWriteFloat32(pResult + kResultScale + 8, zero);
        for (uint32_t offset = 0; offset < 48; offset += 4) {
            MemoryInline::FlatWrite32(pResult + kResultMtx + offset, 0);
        }

        for (int32_t i = 0;
             i < static_cast<int32_t>(MemoryInline::FlatRead32(self + kBlendNumChild)); ++i) {
            const uint32_t slot = static_cast<uint32_t>(i) * 4u;
            const uint32_t child =
                MemoryInline::FlatRead32(MemoryInline::FlatRead32(self + kBlendChildren) + slot);
            const double weight = ReadF32(MemoryInline::FlatRead32(self + kBlendWeights) + slot);
            if (child == 0 || weight == zero) {
                continue;
            }
            const uint32_t binding =
                MemoryInline::FlatRead32(child + kChildBinding) + bindingOffset;
            if ((MemoryInline::FlatRead16(binding) & kBindingSkipMask) != 0) {
                continue;
            }

            InvokeChildGetResult(ctx, child, frame + kFrameChildResult, id);
            const uint32_t childResult = ctx->gpr[3];
            const uint32_t flags = MemoryInline::FlatRead32(childResult + kResultFlags);
            const double ratio = PpcFmulsInline(weight, PPC_PsToScalarInline(invWeightSum));

            if (useAccScale) {
                if ((flags & kChrFlagScaleOne) == 0) {
                    double logX = ReadF32(childResult + kResultScale + 0);
                    logX = (logX > zero) ? GuestFLog(ctx, logX) : logZero;
                    esx = static_cast<double>(
                        PpcForceSingleValueInline(esx + PpcFmulsInline(ratio, logX)));
                    double logY = ReadF32(childResult + kResultScale + 4);
                    logY = (logY > zero) ? GuestFLog(ctx, logY) : logZero;
                    esy = static_cast<double>(
                        PpcForceSingleValueInline(esy + PpcFmulsInline(ratio, logY)));
                    double logZ = ReadF32(childResult + kResultScale + 8);
                    logZ = (logZ > zero) ? GuestFLog(ctx, logZ) : logZero;
                    esz = static_cast<double>(
                        PpcForceSingleValueInline(esz + PpcFmulsInline(ratio, logZ)));
                }
            } else if ((flags & kChrFlagScaleOne) != 0) {
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 0,
                    PpcForceSingleValueInline(ReadF32(pResult + kResultScale + 0) + ratio));
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 4,
                    PpcForceSingleValueInline(ReadF32(pResult + kResultScale + 4) + ratio));
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 8,
                    PpcForceSingleValueInline(ReadF32(pResult + kResultScale + 8) + ratio));
            } else {
                // The guest reads all three accumulators before the first store.
                const double scaleX = ReadF32(pResult + kResultScale + 0);
                const double scaleY = ReadF32(pResult + kResultScale + 4);
                const double scaleZ = ReadF32(pResult + kResultScale + 8);
                const double addX = PpcFmulsInline(ReadF32(childResult + kResultScale + 0), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 0, PpcForceSingleValueInline(scaleX + addX));
                const double addY = PpcFmulsInline(ReadF32(childResult + kResultScale + 4), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 4, PpcForceSingleValueInline(scaleY + addY));
                const double addZ = PpcFmulsInline(ReadF32(childResult + kResultScale + 8), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultScale + 8, PpcForceSingleValueInline(scaleZ + addZ));
            }

            if (useQuat) {
                if ((flags & kChrFlagRotZero) != 0) {
                    MemoryInline::FlatWriteFloat32(frame + kFrameQuat + 0, zero);
                    MemoryInline::FlatWriteFloat32(frame + kFrameQuat + 4, zero);
                    MemoryInline::FlatWriteFloat32(frame + kFrameQuat + 8, zero);
                    MemoryInline::FlatWriteFloat32(frame + kFrameQuat + 12, one);
                } else {
                    ctx->gpr[3] = frame + kFrameQuat;
                    ctx->gpr[4] = childResult + kResultMtx;
                    ctx->lr = 0x80248620u;
                    InvokeDirectCpu<0x801C1680u>(ctx);
                }

                addedWeight =
                    static_cast<double>(PpcForceSingleValueInline(addedWeight + weight));
                const double addedEstimate = PPC_Fres(PPC_PsFromScalarInline(addedWeight));
                const double twiceAdded = PPC_PsAddInline(addedEstimate, addedEstimate);
                const double addedSq = PPC_PsMulInline(addedEstimate, addedEstimate);
                const double invAddedWeight =
                    PPC_PsNmsubInline(PPC_PsFromScalarInline(addedWeight), addedSq, twiceAdded);
                const double t = PpcFmulsInline(weight, PPC_PsToScalarInline(invAddedWeight));

                ctx->gpr[3] = frame + kFrameRot;
                ctx->gpr[4] = frame + kFrameQuat;
                ctx->gpr[5] = frame + kFrameRot;
                ctx->fpr[1].d = t;
                ctx->lr = 0x8024865Cu;
                InvokeDirectCpu<0x801C1850u>(ctx);
            } else if ((flags & kChrFlagRotZero) != 0) {
                if (i == 0) {
                    Mtx33Identity(sda, ctx, frame + kFrameFirstRot);
                }
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx00,
                    PpcForceSingleValueInline(ReadF32(pResult + kResultMtx + kMtx00) + ratio));
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx11,
                    PpcForceSingleValueInline(ReadF32(pResult + kResultMtx + kMtx11) + ratio));
            } else {
                if (i == 0) {
                    constexpr uint32_t kRotElements[9] = {0, 4, 8, 16, 20, 24, 32, 36, 40};
                    for (uint32_t element : kRotElements) {
                        MemoryInline::FlatWriteFloat32(
                            frame + kFrameFirstRot + element,
                            ReadF32(childResult + kResultMtx + element));
                    }
                }
                // Only the first two rows are blended; the third is rebuilt from
                // them by the cross products after the loop.
                const double rot00 = ReadF32(pResult + kResultMtx + kMtx00);
                const double rot01 = ReadF32(pResult + kResultMtx + kMtx01);
                const double rot02 = ReadF32(pResult + kResultMtx + kMtx02);
                const double rot10 = ReadF32(pResult + kResultMtx + kMtx10);
                const double rot11 = ReadF32(pResult + kResultMtx + kMtx11);
                const double rot12 = ReadF32(pResult + kResultMtx + kMtx12);
                const double add00 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx00), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx00, PpcForceSingleValueInline(rot00 + add00));
                const double add01 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx01), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx01, PpcForceSingleValueInline(rot01 + add01));
                const double add02 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx02), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx02, PpcForceSingleValueInline(rot02 + add02));
                const double add10 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx10), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx10, PpcForceSingleValueInline(rot10 + add10));
                const double add11 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx11), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx11, PpcForceSingleValueInline(rot11 + add11));
                const double add12 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx12), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx12, PpcForceSingleValueInline(rot12 + add12));
            }

            if ((flags & kChrFlagTransZero) == 0) {
                const double trans03 = ReadF32(pResult + kResultMtx + kMtx03);
                const double trans13 = ReadF32(pResult + kResultMtx + kMtx13);
                const double trans23 = ReadF32(pResult + kResultMtx + kMtx23);
                const double add03 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx03), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx03, PpcForceSingleValueInline(trans03 + add03));
                const double add13 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx13), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx13, PpcForceSingleValueInline(trans13 + add13));
                const double add23 = PpcFmulsInline(ReadF32(childResult + kResultMtx + kMtx23), ratio);
                MemoryInline::FlatWriteFloat32(
                    pResult + kResultMtx + kMtx23, PpcForceSingleValueInline(trans23 + add23));
            }

            MemoryInline::FlatWrite32(
                pResult + kResultFlags,
                MemoryInline::FlatRead32(pResult + kResultFlags) & flags);
        }

        if (useAccScale) {
            MemoryInline::FlatWriteFloat32(pResult + kResultScale + 0, GuestFExp(ctx, esx));
            MemoryInline::FlatWriteFloat32(pResult + kResultScale + 4, GuestFExp(ctx, esy));
            MemoryInline::FlatWriteFloat32(pResult + kResultScale + 8, GuestFExp(ctx, esz));
        }

        if (useQuat) {
            const double tX = ReadF32(pResult + kResultMtx + kMtx03);
            const double tY = ReadF32(pResult + kResultMtx + kMtx13);
            const double tZ = ReadF32(pResult + kResultMtx + kMtx23);
            MemoryInline::FlatWriteFloat32(frame + kFrameVecT + 0, tX);
            MemoryInline::FlatWriteFloat32(frame + kFrameVecT + 4, tY);
            MemoryInline::FlatWriteFloat32(frame + kFrameVecT + 8, tZ);
            QuatToMtx34(sda, ctx, gqr0, pResult + kResultMtx, frame + kFrameRot);
            MemoryInline::FlatWriteFloat32(pResult + kResultMtx + kMtx03,
                                           ReadF32(frame + kFrameVecT + 0));
            MemoryInline::FlatWriteFloat32(pResult + kResultMtx + kMtx13,
                                           ReadF32(frame + kFrameVecT + 4));
            MemoryInline::FlatWriteFloat32(pResult + kResultMtx + kMtx23,
                                           ReadF32(frame + kFrameVecT + 8));
        } else {
            const uint32_t pV0 = pResult + kResultMtx + kMtx00;
            const uint32_t pV1 = pResult + kResultMtx + kMtx10;
            const uint32_t pV2 = pResult + kResultMtx + kMtx20;
            Vec3Cross(ctx, gqr0, pV0, pV1, pV2);
            bool rebuild = VecLengthSquared(ctx, gqr0, pV0) == zero;
            if (!rebuild) {
                rebuild = VecLengthSquared(ctx, gqr0, pV2) == zero;
            }
            if (rebuild) {
                // Degenerate basis: keep the first blended rotation.
                constexpr uint32_t kRotElements[9] = {0, 4, 8, 16, 20, 24, 32, 36, 40};
                for (uint32_t element : kRotElements) {
                    MemoryInline::FlatWriteFloat32(
                        pResult + kResultMtx + element,
                        ReadF32(frame + kFrameFirstRot + element));
                }
            } else {
                Vec3Normalize(sda, ctx, gqr0, pV0, pV0);
                Vec3Normalize(sda, ctx, gqr0, pV2, pV2);
                Vec3Cross(ctx, gqr0, pV2, pV0, pV1);
            }
        }

        MemoryInline::FlatWrite32(
            pResult + kResultFlags,
            MemoryInline::FlatRead32(pResult + kResultFlags) & ~kChrFlagRotRawFmt);
    }

    ctx->gpr[1] = savedR1;
    ctx->lr = savedLr;
    ctx->gpr[3] = result;
}

struct BlendRegisters {
    uint32_t gpr[32];
    PPC_FPR fpr[32];
    uint32_t cr;
    uint32_t lr;
    uint32_t ctr;
    uint32_t xer;
};

void SaveBlendRegisters(CpuContext* ctx, BlendRegisters& out) {
    std::memcpy(out.gpr, ctx->gpr, sizeof(out.gpr));
    std::memcpy(out.fpr, ctx->fpr, sizeof(out.fpr));
    out.cr = ctx->cr;
    out.lr = ctx->lr;
    out.ctr = ctx->ctr;
    out.xer = ctx->xer;
}

void LoadBlendRegisters(CpuContext* ctx, const BlendRegisters& in) {
    std::memcpy(ctx->gpr, in.gpr, sizeof(in.gpr));
    std::memcpy(ctx->fpr, in.fpr, sizeof(in.fpr));
    ctx->cr = in.cr;
    ctx->lr = in.lr;
    ctx->ctr = in.ctr;
    ctx->xer = in.xer;
}

void ReadChrAnmResult(uint32_t addr, uint32_t (&out)[kResultDwords]) {
    for (uint32_t i = 0; i < kResultDwords; ++i) {
        out[i] = MemoryInline::FlatRead32(addr + i * 4u);
    }
}

void WriteChrAnmResult(uint32_t addr, const uint32_t (&in)[kResultDwords]) {
    for (uint32_t i = 0; i < kResultDwords; ++i) {
        MemoryInline::FlatWrite32(addr + i * 4u, in[i]);
    }
}

void ReadFrame(uint32_t addr, uint32_t (&out)[kFrameSize / 4]) {
    for (uint32_t i = 0; i < kFrameSize / 4; ++i) {
        out[i] = MemoryInline::FlatRead32(addr + i * 4u);
    }
}

void WriteFrame(uint32_t addr, const uint32_t (&in)[kFrameSize / 4]) {
    for (uint32_t i = 0; i < kFrameSize / 4; ++i) {
        MemoryInline::FlatWrite32(addr + i * 4u, in[i]);
    }
}

}  // namespace

extern "C" void NW4R_G3D_AnmObjChrBlendGetResult_HLE(CpuContext* ctx) {
    if (!CompareEnabled()) {
        AnmObjChrBlendGetResultNative(ctx);
        return;
    }

    // Compare mode: run the translated reference on the caller's registers,
    // snapshot the ChrAnmResult it wrote, restore the guest stack frame the child
    // results are built in (children leave parts of that buffer untouched, so both
    // runs must start from the same bytes), replay the same entry state through
    // the native body and compare the two results. Child evaluations are
    // repeated, which only costs time: a child result depends on `id` and the
    // object.
    BlendRegisters entry;
    SaveBlendRegisters(ctx, entry);

    uint32_t frameWords[kFrameSize / 4];
    const uint32_t frameBase = entry.gpr[1] - kFrameSize;
    ReadFrame(frameBase, frameWords);

    AnmObjChrBlendGetResultTranslated(ctx);
    const uint32_t referenceReturn = ctx->gpr[3];
    uint32_t reference[kResultDwords];
    ReadChrAnmResult(entry.gpr[4], reference);

    LoadBlendRegisters(ctx, entry);
    WriteFrame(frameBase, frameWords);
    AnmObjChrBlendGetResultNative(ctx);
    uint32_t native[kResultDwords];
    ReadChrAnmResult(entry.gpr[4], native);

    static uint32_t compares = 0;
    static uint32_t mismatches = 0;
    static uint32_t quatPath = 0;
    static uint32_t accScalePath = 0;
    ++compares;
    const uint32_t anmFlags = MemoryInline::FlatRead32(entry.gpr[3] + kBlendFlags);
    if ((anmFlags & kFlagUseQuatBlend) != 0) {
        ++quatPath;
    }
    if ((anmFlags & kFlagUseAccScale) != 0) {
        ++accScalePath;
    }
    for (uint32_t i = 0; i < kResultDwords; ++i) {
        if (reference[i] == native[i]) {
            continue;
        }
        if (mismatches++ == 0) {
            RT_LOGF(RT_TAG_HLE,
                    "g3d chrblend compare mismatch: this %08X result %08X id %u offset %02X"
                    " translated %08X native %08X\n",
                    entry.gpr[3], entry.gpr[4], entry.gpr[5], i * 4u, reference[i], native[i]);
            const uint32_t obj = entry.gpr[3];
            RT_LOGF(RT_TAG_HLE, "g3d chrblend mm: objflags %08X nChild %u\n",
                    MemoryInline::FlatRead32(obj + kBlendFlags),
                    MemoryInline::FlatRead32(obj + kBlendNumChild));
            for (uint32_t c = 0; c < 4; ++c) {
                RT_LOGF(RT_TAG_HLE, "g3d chrblend mm: child %u %08X weight %.9g\n", c,
                        MemoryInline::FlatRead32(
                            MemoryInline::FlatRead32(obj + kBlendChildren) + c * 4u),
                        ReadF32(MemoryInline::FlatRead32(obj + kBlendWeights) + c * 4u));
            }
            RT_LOG(RT_TAG_HLE) << "g3d chrblend mm ref:";
            for (uint32_t d = 0; d < kResultDwords; ++d) {
                RT_LOG(RT_TAG_HLE) << " " << std::hex << reference[d];
            }
            RT_LOG(RT_TAG_HLE) << "\n";
            RT_LOG(RT_TAG_HLE) << "g3d chrblend mm nat:";
            for (uint32_t d = 0; d < kResultDwords; ++d) {
                RT_LOG(RT_TAG_HLE) << " " << std::hex << native[d];
            }
            RT_LOG(RT_TAG_HLE) << std::dec << "\n";
        }
        break;
    }
    if (compares == 1) {
        RT_LOGF(RT_TAG_HLE,
                "g3d chrblend compare: first call this %08X result %08X id %u\n",
                entry.gpr[3], entry.gpr[4], entry.gpr[5]);
    }
    if (compares % 100000u == 0) {
        RT_LOGF(RT_TAG_HLE,
                "g3d chrblend compare: %u calls, %u mismatches, %u quat, %u accscale\n", compares,
                mismatches, quatPath, accScalePath);
    }

    WriteChrAnmResult(entry.gpr[4], reference);
    LoadBlendRegisters(ctx, entry);
    ctx->gpr[3] = referenceReturn;
}

PPC_NATIVE_OVERRIDE_VOID(80248330, NW4R_G3D_AnmObjChrBlendGetResult_HLE, (CpuContext* ctx), (ctx));
