// Native nw4r::g3d animation result leaves: the per-node rotation and translation curve
// evaluation plus the fixed-point sine/cosine table math both call. Everything here follows
// the guest bodies; the decomp (ogws/math_types.cpp, math_triangular.cpp) supplies the
// matrix formula and the table layout.
//
// 0x802382B0 math::MTX34RotXYZFIdx
// 0x802405B0 GetAnmRotation (const and FVS32 formats native, others fall back)
// 0x80241020 GetAnmTranslation (const and FVS32 formats native, others fall back)
//
// NSMBW_G3D_COMPARE=on runs the translated reference on the same entry registers and compares
// the outputs bit-for-bit, returning the translated result. NSMBW_G3D_NATIVE=0 runs the
// translated bodies directly.
#include "hle_stubs.h"

#include "memory.h"
#include "ppc_runtime.h"
#include "runtime_log.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t kSinCosTable = 0x802F74A0;  // detail::gSinCosTbl, 256 x 16 bytes
constexpr uint32_t kFidxScaleSda = 19160;      // SDA float: degrees to table index

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

bool NativeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_G3D_NATIVE");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

float ReadF32(uint32_t addr) {
    return MemoryInline::FlatReadFloat32(addr);
}

// math::SinCosFIdx: range reduce in table units, look up the sample and interpolate.
void SinCosFIdx(float fidx, float* outSin, float* outCos) {
    float magnitude = std::fabs(fidx);
    while (magnitude > 65536.0f) {
        magnitude -= 65536.0f;
    }
    const uint16_t whole = static_cast<uint16_t>(static_cast<int32_t>(magnitude));
    const float frac = magnitude - static_cast<float>(whole);
    const uint32_t entry = kSinCosTable + (whole & 0xFFu) * 16u;
    const float sinValue = ReadF32(entry);
    const float cosValue = ReadF32(entry + 4);
    const float sinDelta = ReadF32(entry + 8);
    const float cosDelta = ReadF32(entry + 12);
    const float sine = std::fma(frac, sinDelta, sinValue);
    *outCos = std::fma(frac, cosDelta, cosValue);
    *outSin = (fidx < 0.0f) ? -sine : sine;
}

// math::MTX34RotXYZFIdx, operation order from the guest body.
void RotXYZFIdxNative(uint32_t mtx, float fx, float fy, float fz) {
    float sx, cx, sy, cy, sz, cz;
    SinCosFIdx(fx, &sx, &cx);
    SinCosFIdx(fy, &sy, &cy);
    SinCosFIdx(fz, &sz, &cz);

    const float cx_sz = cx * sz;
    const float sx_cz = sx * cz;
    const float sx_sz = sx * sz;
    const float cx_cz = cx * cz;

    MemoryInline::FlatWriteFloat32(mtx + 0x00, cz * cy);
    MemoryInline::FlatWriteFloat32(mtx + 0x04, std::fma(sx_cz, sy, -cx_sz));
    MemoryInline::FlatWriteFloat32(mtx + 0x08, std::fma(cx_cz, sy, sx_sz));
    MemoryInline::FlatWriteFloat32(mtx + 0x0C, 0.0f);
    MemoryInline::FlatWriteFloat32(mtx + 0x10, sz * cy);
    MemoryInline::FlatWriteFloat32(mtx + 0x14, std::fma(sx_sz, sy, cx_cz));
    MemoryInline::FlatWriteFloat32(mtx + 0x18, std::fma(cx_sz, sy, -sx_cz));
    MemoryInline::FlatWriteFloat32(mtx + 0x1C, 0.0f);
    MemoryInline::FlatWriteFloat32(mtx + 0x20, -sy);
    MemoryInline::FlatWriteFloat32(mtx + 0x24, cy * sx);
    MemoryInline::FlatWriteFloat32(mtx + 0x28, cy * cx);
    MemoryInline::FlatWriteFloat32(mtx + 0x2C, 0.0f);
}

struct SavedRegisters {
    uint32_t gpr[32];
    PPC_FPR fpr[32];
    uint32_t cr;
    uint32_t lr;
    uint32_t ctr;
    uint32_t xer;
};

void SaveRegisters(CpuContext* ctx, SavedRegisters& saved) {
    std::memcpy(saved.gpr, ctx->gpr, sizeof(saved.gpr));
    std::memcpy(saved.fpr, ctx->fpr, sizeof(saved.fpr));
    saved.cr = ctx->cr;
    saved.lr = ctx->lr;
    saved.ctr = ctx->ctr;
    saved.xer = ctx->xer;
}

void LoadRegisters(CpuContext* ctx, const SavedRegisters& saved) {
    std::memcpy(ctx->gpr, saved.gpr, sizeof(saved.gpr));
    std::memcpy(ctx->fpr, saved.fpr, sizeof(saved.fpr));
    ctx->cr = saved.cr;
    ctx->lr = saved.lr;
    ctx->ctr = saved.ctr;
    ctx->xer = saved.xer;
}

}  // namespace

extern "C" void NW4R_G3D_RotXYZFIdx_Translated(CpuContext* ctx);
extern "C" void NW4R_G3D_AnmRotation_Translated(CpuContext* ctx);
extern "C" void NW4R_G3D_AnmTranslation_Translated(CpuContext* ctx);
extern "C" void NW4R_G3D_CalcAnimationFVS32_HLE(CpuContext* ctx);

// The translated bodies were emitted inside a shard, where this macro is a direct call.
extern "C" void func_80240100(CpuContext* ctx);
extern "C" void func_80240240(CpuContext* ctx);
#define MKW_STATIC_TRANSLATED_CALL(Target, Entry, Context) \
    do { ApplyRuntimeCallOptions(Target, Context); Entry(Context); } while (false)

#include "nw4r_g3d_rotxyz_translated.inc"

extern "C" void NW4R_G3D_RotXYZFIdx_HLE(CpuContext* ctx) {
    const uint32_t mtx = ctx->gpr[3];
    const float fx = static_cast<float>(ctx->fpr[1].d);
    const float fy = static_cast<float>(ctx->fpr[2].d);
    const float fz = static_cast<float>(ctx->fpr[3].d);
    if (!NativeEnabled()) {
        NW4R_G3D_RotXYZFIdx_Translated(ctx);
        return;
    }
    if (!CompareEnabled()) {
        RotXYZFIdxNative(mtx, fx, fy, fz);
        return;
    }

    SavedRegisters entry;
    SaveRegisters(ctx, entry);
    NW4R_G3D_RotXYZFIdx_Translated(ctx);
    float reference[12];
    for (uint32_t i = 0; i < 12; ++i) {
        reference[i] = ReadF32(mtx + i * 4u);
    }
    LoadRegisters(ctx, entry);
    RotXYZFIdxNative(mtx, fx, fy, fz);

    bool mismatch = false;
    for (uint32_t i = 0; i < 12; ++i) {
        const float native = ReadF32(mtx + i * 4u);
        if (std::memcmp(&native, &reference[i], sizeof(float)) != 0) {
            mismatch = true;
            break;
        }
    }
    static uint32_t compares = 0;
    static uint32_t mismatches = 0;
    ++compares;
    if (mismatch && mismatches++ == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d rotxyz compare mismatch: mtx %08X (%.9g %.9g %.9g)\n", mtx, fx, fy,
                fz);
    }
    if (compares % 100000u == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d rotxyz compare: %u calls, %u mismatches\n", compares, mismatches);
    }
    if (mismatch) {
        for (uint32_t i = 0; i < 12; ++i) {
            MemoryInline::FlatWriteFloat32(mtx + i * 4u, reference[i]);
        }
    }
}

PPC_NATIVE_OVERRIDE_VOID(802382B0, NW4R_G3D_RotXYZFIdx_HLE, (CpuContext* ctx), (ctx));

namespace {

// ResAnmChrNodeData flags at +0x04.
constexpr uint32_t kNodeFlags = 0x04;
constexpr uint32_t kRotFormatMask = 0x38000000u;
constexpr uint32_t kRotFvs32 = 0x08000000u;
constexpr uint32_t kTransFormatMask = 0xC0000000u;
constexpr uint32_t kTransFvs32 = 0x40000000u;

bool IsFvs32Format(uint32_t flags, uint32_t mask, uint32_t fvs32) {
    const uint32_t format = flags & mask;
    return format == 0u || format == fvs32;
}

// Evaluates one FVS32 curve at `frame` through the native curve leaf.
float Fvs32Axis(CpuContext* ctx, uint32_t node, uint32_t data, double frame) {
    ctx->gpr[3] = node + data;
    ctx->fpr[1].d = frame;
    NW4R_G3D_CalcAnimationFVS32_HLE(ctx);
    return static_cast<float>(ctx->fpr[1].d);
}

uint32_t AnmRotationNative(CpuContext* ctx) {
    const uint32_t mtx = ctx->gpr[3];
    const uint32_t raw = ctx->gpr[4];
    const uint32_t node = ctx->gpr[6];
    uint32_t anm = ctx->gpr[7];
    const double frame = ctx->fpr[1].d;
    const uint32_t flags = MemoryInline::FlatRead32(node + kNodeFlags);

    float x, y, z;
    if ((flags & kRotFormatMask) == 0u) {
        x = ReadF32(anm);
        y = ReadF32(anm + 4);
        z = ReadF32(anm + 8);
        anm += 12;
    } else {
        float axes[3];
        for (uint32_t axis = 0; axis < 3; ++axis) {
            if ((flags & (1u << (16 + axis))) != 0u) {
                axes[axis] = ReadF32(anm);
            } else {
                axes[axis] = Fvs32Axis(ctx, node, MemoryInline::FlatRead32(anm), frame);
            }
            anm += 4;
        }
        x = axes[0];
        y = axes[1];
        z = axes[2];
    }

    const float fidxScale = ReadF32(ctx->gpr[2] - kFidxScaleSda);
    RotXYZFIdxNative(mtx, static_cast<float>(PpcFmulsInline(fidxScale, x)),
                     static_cast<float>(PpcFmulsInline(fidxScale, y)),
                     static_cast<float>(PpcFmulsInline(fidxScale, z)));
    MemoryInline::FlatWriteFloat32(raw, x);
    MemoryInline::FlatWriteFloat32(raw + 4, y);
    MemoryInline::FlatWriteFloat32(raw + 8, z);
    return anm;
}

uint32_t AnmTranslationNative(CpuContext* ctx) {
    const uint32_t trans = ctx->gpr[3];
    const uint32_t node = ctx->gpr[4];
    uint32_t anm = ctx->gpr[5];
    const double frame = ctx->fpr[1].d;
    const uint32_t flags = MemoryInline::FlatRead32(node + kNodeFlags);

    float x, y, z;
    if ((flags & kTransFormatMask) == 0u) {
        x = ReadF32(anm);
        y = ReadF32(anm + 4);
        z = ReadF32(anm + 8);
        anm += 12;
    } else {
        float axes[3];
        for (uint32_t axis = 0; axis < 3; ++axis) {
            if ((flags & (1u << (19 + axis))) != 0u) {
                axes[axis] = ReadF32(anm);
            } else {
                axes[axis] = Fvs32Axis(ctx, node, MemoryInline::FlatRead32(anm), frame);
            }
            anm += 4;
        }
        x = axes[0];
        y = axes[1];
        z = axes[2];
    }

    MemoryInline::FlatWriteFloat32(trans, x);
    MemoryInline::FlatWriteFloat32(trans + 4, y);
    MemoryInline::FlatWriteFloat32(trans + 8, z);
    return anm;
}

}  // namespace

#include "nw4r_g3d_anmrot_translated.inc"
#include "nw4r_g3d_anmtrans_translated.inc"

#undef MKW_STATIC_TRANSLATED_CALL

extern "C" void NW4R_G3D_AnmRotation_HLE(CpuContext* ctx) {
    const uint32_t flags = MemoryInline::FlatRead32(ctx->gpr[6] + kNodeFlags);
    if (!NativeEnabled() || !IsFvs32Format(flags, kRotFormatMask, kRotFvs32)) {
        NW4R_G3D_AnmRotation_Translated(ctx);
        return;
    }
    if (!CompareEnabled()) {
        ctx->gpr[3] = AnmRotationNative(ctx);
        return;
    }

    const uint32_t mtx = ctx->gpr[3];
    const uint32_t raw = ctx->gpr[4];
    SavedRegisters entry;
    SaveRegisters(ctx, entry);
    NW4R_G3D_AnmRotation_Translated(ctx);
    const uint32_t referenceReturn = ctx->gpr[3];
    float reference[12];
    float referenceRaw[3];
    for (uint32_t i = 0; i < 12; ++i) {
        reference[i] = ReadF32(mtx + i * 4u);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        referenceRaw[i] = ReadF32(raw + i * 4u);
    }
    LoadRegisters(ctx, entry);

    const uint32_t nativeReturn = AnmRotationNative(ctx);
    bool mismatch = nativeReturn != referenceReturn;
    for (uint32_t i = 0; i < 12 && !mismatch; ++i) {
        const float native = ReadF32(mtx + i * 4u);
        mismatch = std::memcmp(&native, &reference[i], sizeof(float)) != 0;
    }
    for (uint32_t i = 0; i < 3 && !mismatch; ++i) {
        const float native = ReadF32(raw + i * 4u);
        mismatch = std::memcmp(&native, &referenceRaw[i], sizeof(float)) != 0;
    }
    static uint32_t compares = 0;
    static uint32_t mismatches = 0;
    ++compares;
    if (mismatch && mismatches++ == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d anmrot compare mismatch: node %08X anm %u flags %08X\n",
                entry.gpr[6], entry.gpr[7], flags);
    }
    if (compares % 100000u == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d anmrot compare: %u calls, %u mismatches\n", compares, mismatches);
    }
    if (mismatch) {
        for (uint32_t i = 0; i < 12; ++i) {
            MemoryInline::FlatWriteFloat32(mtx + i * 4u, reference[i]);
        }
        for (uint32_t i = 0; i < 3; ++i) {
            MemoryInline::FlatWriteFloat32(raw + i * 4u, referenceRaw[i]);
        }
        ctx->gpr[3] = referenceReturn;
    } else {
        ctx->gpr[3] = nativeReturn;
    }
}

PPC_NATIVE_OVERRIDE_VOID(802405B0, NW4R_G3D_AnmRotation_HLE, (CpuContext* ctx), (ctx));

extern "C" void NW4R_G3D_AnmTranslation_HLE(CpuContext* ctx) {
    const uint32_t flags = MemoryInline::FlatRead32(ctx->gpr[4] + kNodeFlags);
    if (!NativeEnabled() || !IsFvs32Format(flags, kTransFormatMask, kTransFvs32)) {
        NW4R_G3D_AnmTranslation_Translated(ctx);
        return;
    }
    if (!CompareEnabled()) {
        ctx->gpr[3] = AnmTranslationNative(ctx);
        return;
    }

    const uint32_t trans = ctx->gpr[3];
    SavedRegisters entry;
    SaveRegisters(ctx, entry);
    NW4R_G3D_AnmTranslation_Translated(ctx);
    const uint32_t referenceReturn = ctx->gpr[3];
    float reference[3];
    for (uint32_t i = 0; i < 3; ++i) {
        reference[i] = ReadF32(trans + i * 4u);
    }
    LoadRegisters(ctx, entry);

    const uint32_t nativeReturn = AnmTranslationNative(ctx);
    bool mismatch = nativeReturn != referenceReturn;
    for (uint32_t i = 0; i < 3 && !mismatch; ++i) {
        const float native = ReadF32(trans + i * 4u);
        mismatch = std::memcmp(&native, &reference[i], sizeof(float)) != 0;
    }
    static uint32_t compares = 0;
    static uint32_t mismatches = 0;
    ++compares;
    if (mismatch && mismatches++ == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d anmtrans compare mismatch: node %08X anm %u flags %08X\n",
                entry.gpr[4], entry.gpr[5], flags);
    }
    if (compares % 100000u == 0) {
        RT_LOGF(RT_TAG_HLE, "g3d anmtrans compare: %u calls, %u mismatches\n", compares,
                mismatches);
    }
    if (mismatch) {
        for (uint32_t i = 0; i < 3; ++i) {
            MemoryInline::FlatWriteFloat32(trans + i * 4u, reference[i]);
        }
        ctx->gpr[3] = referenceReturn;
    } else {
        ctx->gpr[3] = nativeReturn;
    }
}

PPC_NATIVE_OVERRIDE_VOID(80241020, NW4R_G3D_AnmTranslation_HLE, (CpuContext* ctx), (ctx));
