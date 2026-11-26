// NSMBW water surface: d3d::rtexQuadManyTwin_c::drawTexQuadManyTwinNormal (0x8000B530).
//
// The guest draws one GXBegin(GX_QUADS) per quad, with every vertex component written through the
// HLE FIFO. Nothing changes GX state between quads, so the native path repeats the guest's setup
// and draws the whole surface as merged GX_QUADS batches. The vertex bytes use the same float
// conversions and big-endian layout as the translated loop.
//
// The native dispatcher at 0x8000D000 also preserves the other surface kinds: magma, poison and
// sand are still dispatched to their translated functions. NSMBW_WATERHLE=0 restores the
// translated normal-water path.

#include "abi_bridge.h"
#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <aurora/aurora.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t* data, uint32_t sizeBytes);

namespace {

constexpr uint32_t kDrawNormal = 0x8000B530u;
constexpr uint32_t kDrawMagma = 0x8000BA70u;
constexpr uint32_t kDrawMagmaHi = 0x8000C5C0u;
constexpr uint32_t kDrawSand = 0x8000C020u;
constexpr uint32_t kDrawPoison = 0x8000CA50u;
constexpr uint32_t kResetMaterial = 0x80164F50u;
constexpr uint32_t kGetCurrentCamera = 0x80164B30u;
constexpr uint32_t kGetCameraMtx = 0x802540B0u;
constexpr uint32_t kGetViewMtx = 0x8016A190u;
constexpr uint32_t kPsMtxConcat = 0x801C0540u;
constexpr uint32_t kGxSetFogRangeAdj = 0x801C8CB0u;
constexpr uint32_t kGxBegin = 0x801C5570u;

// rtexQuadManyTwin_c layout, shared by every kind.
constexpr uint32_t kTexObj0 = 8;
constexpr uint32_t kTexObj1 = 40;
constexpr uint32_t kCornerA = 72;
constexpr uint32_t kCornerB = 13272;
constexpr uint32_t kCornerC = 26472;
constexpr uint32_t kCornerD = 39672;
constexpr uint32_t kColorTop = 52872;
constexpr uint32_t kColorBottom = 52876;
constexpr uint32_t kQuadCount = 52880;
constexpr uint32_t kKind = 52884;
constexpr uint32_t kTexOfs0 = 52888;
constexpr uint32_t kTexOfs1 = 52890;

constexpr uint32_t kVertexBytes = 24u;  // POS xyz f32, CLR0 RGBA8, TEX0 st s16, TEX1 st s16
constexpr int32_t kMaxQuadsPerBegin = 0xFFFF / 4;

bool WaterHleEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_WATERHLE");
        const bool on = value == nullptr || value[0] != '0';
        if (on) {
            RT_LOGF(RT_TAG_GX, "NSMBW_WATERHLE: water surface as merged quad batches\n");
        }
        return on;
    }();
    return enabled;
}

void SetArgs(CpuContext* ctx, std::initializer_list<uint32_t> args) {
    uint32_t reg = 3;
    for (const uint32_t arg : args) {
        ctx->gpr[reg++] = arg;
    }
}

void PutBe32(uint8_t*& out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
    out += 4;
}

void PutBe16(uint8_t*& out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value);
    out += 2;
}

void PutGuestFloat(uint8_t*& out, uint32_t address) {
    const double loaded = MemoryInline::FlatReadFloat32(address);
    const float written = static_cast<float>(loaded);
    uint32_t bits = 0;
    std::memcpy(&bits, &written, sizeof(bits));
    PutBe32(out, bits);
}

// One vertex as the guest loop writes it: position, colour, then TEX0 and TEX1.
void PutVertex(uint8_t*& out, uint32_t position, uint32_t color, int32_t s0, int32_t s1, int32_t t) {
    PutGuestFloat(out, position);
    PutGuestFloat(out, position + 4);
    PutGuestFloat(out, position + 8);
    PutBe32(out, color);
    PutBe16(out, static_cast<uint32_t>(s0));
    PutBe16(out, static_cast<uint32_t>(t));
    PutBe16(out, static_cast<uint32_t>(s1));
    PutBe16(out, static_cast<uint32_t>(t));
}

void DrawQuads(CpuContext* ctx, uint32_t self) {
    const int32_t count = static_cast<int32_t>(MemoryInline::FlatRead32(self + kQuadCount));
    if (count <= 0) {
        return;
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        RT_LOGF(RT_TAG_GX, "NSMBW_WATERHLE: first water surface, %d quads in merged batches\n", count);
    }
    const uint32_t colorTop = MemoryInline::FlatRead32(self + kColorTop);
    const uint32_t colorBottom = MemoryInline::FlatRead32(self + kColorBottom);
    const int32_t ofs0 = static_cast<int16_t>(MemoryInline::FlatRead16(self + kTexOfs0));
    const int32_t ofs1 = static_cast<int16_t>(MemoryInline::FlatRead16(self + kTexOfs1));
    constexpr int32_t kTop = 0;
    constexpr int32_t kBottom = 1023;
    static std::vector<uint8_t> bytes;
    for (int32_t first = 0; first < count; first += kMaxQuadsPerBegin) {
        const int32_t quads = std::min(count - first, kMaxQuadsPerBegin);
        bytes.resize(static_cast<size_t>(quads) * 4u * kVertexBytes);
        uint8_t* out = bytes.data();
        for (int32_t i = first; i < first + quads; ++i) {
            const int32_t left = (i % 11) * 93;
            const int32_t right = left + 92;
            const uint32_t step = static_cast<uint32_t>(i) * 12u;
            PutVertex(out, self + kCornerA + step, colorTop, left + ofs0, left + ofs1, kTop);
            PutVertex(out, self + kCornerB + step, colorTop, right + ofs0, right + ofs1, kTop);
            PutVertex(out, self + kCornerC + step, colorBottom, right + ofs0, right + ofs1, kBottom);
            PutVertex(out, self + kCornerD + step, colorBottom, left + ofs0, left + ofs1, kBottom);
        }
        SetArgs(ctx, {0x80u /* GX_QUADS */, 0u /* GX_VTXFMT0 */,
                      static_cast<uint32_t>(quads) * 4u});
        InvokeDirectCpu<kGxBegin>(ctx);
        GX_HLE_FIFO_WriteBurst(bytes.data(), static_cast<uint32_t>(bytes.size()));
    }
}

// drawTexQuadManyTwinNormal (0x8000B530), call for call up to the quad loop.
void DrawNormalHle(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const uint32_t callerSp = ctx->gpr[1];
    const uint32_t r2 = ctx->gpr[2];

    // stwu r1, -160(r1); mflr r0; stw r0, 164(r1); _savegpr_21
    const uint32_t sp = callerSp - 160u;
    MemoryInline::FlatWriteRam32(sp, callerSp);
    MemoryInline::FlatWriteRam32(sp + 164u, ctx->lr);
    for (uint32_t reg = 21; reg <= 31; ++reg) {
        MemoryInline::FlatWriteRam32(sp + 116u + (reg - 21u) * 4u, ctx->gpr[reg]);
    }
    ctx->gpr[1] = sp;
    ctx->gpr[21] = self;

    SetArgs(ctx, {self + kTexObj0, 0});
    ctx->lr = 0x8000B554u;
    InvokeDirectCpu<0x801C74C0u>(ctx);  // GXLoadTexObj(&mTexObj0, GX_TEXMAP0)
    SetArgs(ctx, {self + kTexObj1, 1});
    ctx->lr = 0x8000B560u;
    InvokeDirectCpu<0x801C74C0u>(ctx);  // GXLoadTexObj(&mTexObj1, GX_TEXMAP1)
    InvokeDirectCpu<0x801C4070u>(ctx);  // GXClearVtxDesc
    SetArgs(ctx, {9, 1});
    InvokeDirectCpu<0x801C37C0u>(ctx);  // GX_VA_POS
    SetArgs(ctx, {11, 1});
    InvokeDirectCpu<0x801C37C0u>(ctx);  // GX_VA_CLR0
    SetArgs(ctx, {13, 1});
    InvokeDirectCpu<0x801C37C0u>(ctx);  // GX_VA_TEX0
    SetArgs(ctx, {14, 1});
    InvokeDirectCpu<0x801C37C0u>(ctx);  // GX_VA_TEX1
    SetArgs(ctx, {0, 9, 1, 4, 0});
    InvokeDirectCpu<0x801C40B0u>(ctx);  // POS, XYZ, F32
    SetArgs(ctx, {0, 11, 1, 5, 0});
    InvokeDirectCpu<0x801C40B0u>(ctx);  // CLR0, RGBA, RGBA8
    SetArgs(ctx, {0, 13, 1, 3, 10});
    InvokeDirectCpu<0x801C40B0u>(ctx);  // TEX0, ST, S16
    SetArgs(ctx, {0, 14, 1, 3, 10});
    InvokeDirectCpu<0x801C40B0u>(ctx);  // TEX1, ST, S16
    ctx->lr = 0x8000B5F8u;
    InvokeIndirectCpu(kResetMaterial, ctx);

    SetArgs(ctx, {1});
    InvokeDirectCpu<0x801C6AC0u>(ctx);  // GXSetNumChans(1)
    SetArgs(ctx, {4, 0, 1, 1, 0, 0, 2});
    InvokeDirectCpu<0x801C6AF0u>(ctx);  // GXSetChanCtrl
    SetArgs(ctx, {2});
    InvokeDirectCpu<0x801C4A20u>(ctx);  // GXSetNumTexGens(2)
    SetArgs(ctx, {0, 1, 4, 60, 0, 125});
    ctx->lr = 0x8000B644u;
    InvokeDirectCpu<0x801C47D0u>(ctx);  // TEXCOORD0
    SetArgs(ctx, {1, 1, 5, 60, 0, 125});
    ctx->lr = 0x8000B660u;
    InvokeDirectCpu<0x801C47D0u>(ctx);  // TEXCOORD1
    SetArgs(ctx, {2});
    InvokeDirectCpu<0x801C88F0u>(ctx);  // GXSetNumTevStages(2)
    SetArgs(ctx, {0});
    InvokeDirectCpu<0x801C8110u>(ctx);  // GXSetNumIndStages(0)
    SetArgs(ctx, {0, 0, 1, 2, 3});
    InvokeDirectCpu<0x801C8640u>(ctx);  // GXSetTevSwapModeTable
    SetArgs(ctx, {0, 0, 0, 4});
    InvokeDirectCpu<0x801C8790u>(ctx);  // GXSetTevOrder(0, ...)
    SetArgs(ctx, {0, 15, 15, 15, 8});
    InvokeDirectCpu<0x801C82F0u>(ctx);  // GXSetTevColorIn(0)
    SetArgs(ctx, {0, 0, 0, 0, 1, 0});
    InvokeDirectCpu<0x801C8370u>(ctx);  // GXSetTevColorOp(0)
    SetArgs(ctx, {0, 7, 5, 4, 7});
    InvokeDirectCpu<0x801C8330u>(ctx);  // GXSetTevAlphaIn(0)
    SetArgs(ctx, {0, 0, 0, 0, 1, 0});
    InvokeDirectCpu<0x801C83D0u>(ctx);  // GXSetTevAlphaOp(0)
    SetArgs(ctx, {1, 1, 1, 4});
    InvokeDirectCpu<0x801C8790u>(ctx);  // GXSetTevOrder(1, ...)
    SetArgs(ctx, {1, 15, 0, 8, 10});
    InvokeDirectCpu<0x801C82F0u>(ctx);  // GXSetTevColorIn(1)
    SetArgs(ctx, {1, 0, 0, 0, 1, 0});
    InvokeDirectCpu<0x801C8370u>(ctx);  // GXSetTevColorOp(1)
    SetArgs(ctx, {1, 7, 0, 4, 7});
    InvokeDirectCpu<0x801C8330u>(ctx);  // GXSetTevAlphaIn(1)
    SetArgs(ctx, {1, 0, 0, 0, 1, 0});
    InvokeDirectCpu<0x801C83D0u>(ctx);  // GXSetTevAlphaOp(1)
    SetArgs(ctx, {0});
    InvokeDirectCpu<0x801C8EB0u>(ctx);  // GXSetZCompLoc(GX_FALSE)
    SetArgs(ctx, {1, 4, 5, 15});
    InvokeDirectCpu<0x801C8DC0u>(ctx);  // GXSetBlendMode
    SetArgs(ctx, {1, 3, 0});
    InvokeDirectCpu<0x801C8E70u>(ctx);  // GXSetZMode
    SetArgs(ctx, {4, 0, 1, 4, 0});
    InvokeDirectCpu<0x801C86C0u>(ctx);  // GXSetAlphaCompare
    SetArgs(ctx, {1});
    InvokeDirectCpu<0x801C9D60u>(ctx);  // GXSetClipMode(GX_CLIP_DISABLE)

    // GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, {0}).
    const double zero = MemoryInline::FlatReadFloat32(r2 - 32736u);
    ctx->gpr[27] = 0;
    MemoryInline::FlatWriteRam32(sp + 8u, 0);
    ctx->fpr[1].d = zero;
    ctx->fpr[2].d = zero;
    ctx->fpr[3].d = zero;
    ctx->fpr[4].d = zero;
    SetArgs(ctx, {0, sp + 8u});
    InvokeDirectCpu<0x801C8920u>(ctx);
    SetArgs(ctx, {0, 0, 0});
    InvokeIndirectCpu(kGxSetFogRangeAdj, ctx);
    SetArgs(ctx, {0});
    InvokeDirectCpu<0x801C5860u>(ctx);  // GXSetCullMode(GX_CULL_NONE)
    SetArgs(ctx, {1});
    InvokeDirectCpu<0x801C8F90u>(ctx);  // GXSetDither(GX_TRUE)

    ctx->lr = 0x8000B814u;
    InvokeIndirectCpu(kGetCurrentCamera, ctx);
    MemoryInline::FlatWriteRam32(sp + 12u, ctx->gpr[3]);
    SetArgs(ctx, {sp + 12u, sp + 64u});
    ctx->lr = 0x8000B824u;
    InvokeIndirectCpu(kGetCameraMtx, ctx);
    const double zeroAgain = MemoryInline::FlatReadFloat32(r2 - 32736u);
    ctx->fpr[0].d = zeroAgain;
    MemoryInline::FlatWriteRamFloat32(sp + 76u, zeroAgain);
    MemoryInline::FlatWriteRamFloat32(sp + 92u, zeroAgain);
    MemoryInline::FlatWriteRamFloat32(sp + 108u, zeroAgain);
    SetArgs(ctx, {self, sp + 16u});
    ctx->lr = 0x8000B840u;
    InvokeIndirectCpu(kGetViewMtx, ctx);
    SetArgs(ctx, {sp + 16u, sp + 64u, sp + 16u});
    InvokeIndirectCpu(kPsMtxConcat, ctx);
    SetArgs(ctx, {sp + 16u, 0});
    InvokeDirectCpu<0x801C9940u>(ctx);  // GXLoadPosMtxImm
    SetArgs(ctx, {0});
    InvokeDirectCpu<0x801C9A60u>(ctx);  // GXSetCurrentMtx

    ctx->gpr[22] = 0;
    ctx->gpr[28] = self + 0x10000u;
    DrawQuads(ctx, self);

    SetArgs(ctx, {0});
    InvokeDirectCpu<0x801C9D60u>(ctx);  // GXSetClipMode(GX_CLIP_ENABLE)

    for (uint32_t reg = 21; reg <= 31; ++reg) {
        ctx->gpr[reg] = MemoryInline::FlatRead32(sp + 116u + (reg - 21u) * 4u);
    }
    const uint32_t lr = MemoryInline::FlatRead32(sp + 164u);
    ctx->gpr[0] = lr;
    ctx->lr = lr;
    ctx->gpr[11] = callerSp;
    ctx->gpr[1] = callerSp;
}

void CallTranslated(CpuContext* ctx, uint32_t target, uint32_t r0, uint32_t r4, uint32_t r5, uint32_t cr) {
    ctx->gpr[0] = r0;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->cr = cr;
    InvokeIndirectCpu(target, ctx);
}

} // namespace

extern "C" void RtexQuadManyTwinDraw_HLE_8000d000(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    uint32_t cr = ctx->cr;
    const uint32_t xer = ctx->xer;
    uint32_t r4 = self + 0x10000u;
    const uint32_t r5 = MemoryInline::FlatRead32(ctx->gpr[13] - 21688u);
    uint32_t r0 = MemoryInline::FlatRead32(r4 - 12652u);
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), 0);
    if (r0 == 0) {
        if (WaterHleEnabled()) {
            ctx->gpr[0] = r0;
            ctx->gpr[4] = r4;
            ctx->gpr[5] = r5;
            ctx->cr = cr;
            DrawNormalHle(ctx);
        } else {
            CallTranslated(ctx, kDrawNormal, r0, r4, r5, cr);
        }
        return;
    }
    if (r0 == 1) {
        r4 = MemoryInline::FlatRead8(r5 + 4620u);
        bool hi = false;
        if (r4 == 7) {
            r0 = MemoryInline::FlatRead8(r5 + 4621u);
            SetCRResident(cr, xer, 0, r0, 2u);
            hi = r0 == 2;
        }
        if (!hi && r4 == 7) {
            r0 = MemoryInline::FlatRead8(r5 + 4621u);
            if (r0 == 23) {
                r0 = MemoryInline::FlatRead8(r5 + 4622u);
                SetCRResident(cr, xer, 0, r0, 3u);
                hi = r0 == 3;
            }
        }
        if (!hi) {
            SetCRResident(cr, xer, 0, r4, 1u);
            if (r4 == 1) {
                r0 = MemoryInline::FlatRead8(r5 + 4621u);
                SetCRResident(cr, xer, 0, r0, 23u);
                if (r0 == 23) {
                    r0 = MemoryInline::FlatRead8(r5 + 4622u);
                    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), 0);
                    hi = r0 == 0;
                }
            }
        }
        CallTranslated(ctx, hi ? kDrawMagmaHi : kDrawMagma, r0, r4, r5, cr);
        return;
    }
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), 2);
    if (r0 == 2) {
        CallTranslated(ctx, kDrawPoison, r0, r4, r5, cr);
        return;
    }
    SetCRResident(cr, xer, 0, static_cast<int32_t>(r0), 4);
    if (r0 == 4) {
        CallTranslated(ctx, kDrawSand, r0, r4, r5, cr);
    }
}
PPC_NATIVE_OVERRIDE_VOID(8000D000, RtexQuadManyTwinDraw_HLE_8000d000, (CpuContext* ctx), (ctx));
