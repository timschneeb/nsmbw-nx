// gx_vertex.cpp - Vertex Descriptor and Attribute Functions
#include "det_clock.h"
#include "gx_internal.h"
#include "gx_stream_common.h"
#include "fiber_manager.h"
#include "guest_interrupt_context.h"
#include "runtime_log.h"

#include <dolphin/gx/GXAurora.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
uint32_t CanonicalVtxAttr(uint32_t attr) {
    return attr == GX_VA_NBT ? GX_VA_NRM : attr;
}

// There is deliberately no indexed-aurora path here. The immediate begin path
// streams vertex data incrementally, and aurora's indexed-array upload must
// precede the draw carrying the max-index bounds, which cannot be known without
// buffering the whole primitive. Indexed attributes are therefore expanded into
// the packed direct stream below (GX_INDEX8/16 -> GX_DIRECT).
// NSMBW: every GXBegin (each background tile, each text glyph) republished the whole vertex
// descriptor and format, 40-odd GX calls that almost always change nothing. Skipped when the HLE's
// vertex state is the one last published for this format and aurora still holds exactly what that
// publish left (anything else that set aurora's descriptor or table shows up as different registers).

namespace {

struct BeginPublishMemo {
    bool valid = false;
    uint32_t generation = 0;
    uint32_t regs[6] = {};
    bool vatValid = false;
    VtxAttrFmt vat[26] = {};
};

BeginPublishMemo g_beginPublish[GX_MAX_VTXFMT];

bool SameVatFmt(const VtxAttrFmt& a, const VtxAttrFmt& b) {
    return a.cnt == b.cnt && a.type == b.type && a.frac == b.frac;
}

// The pre-per-format memo, kept for NSMBW_GX_FMT_MEMO=0 and for A/B runs.
void ApplyAuroraVtxStateForBeginLegacy(GXVtxFmt fmt) {
    static bool valid = false;
    static GXVtxFmt lastFmt = GX_VTXFMT0;
    static uint32_t lastGeneration = 0;
    static uint32_t lastRegs[6] = {};
    uint32_t regs[6];
    AuroraGetVtxFormatRegs(fmt, regs);
    const bool samePublish =
        valid && fmt == lastFmt && g_hleGxState.vtxStateGeneration == lastGeneration;
    if (samePublish && std::memcmp(regs, lastRegs, sizeof(regs)) == 0) {
        return;
    }
    // regs[0]/[1] are aurora's vcdLo/vcdHi. When the HLE state has not moved since the last
    // publish and aurora still holds that descriptor, only the VAT row needs the republish.
    static const bool descSkip = [] {
        const char* value = std::getenv("NSMBW_GX_DESC_SKIP");
        return value == nullptr || value[0] != '0';
    }();
    const bool descSame = descSkip && samePublish && regs[0] == lastRegs[0] &&
                          regs[1] == lastRegs[1] && regs[5] == lastRegs[5];
    // An NBT format write between begins changes only the packed NRM VAT. When everything else is
    // still the last published state, one NRM republish reproduces the full row.
    static const bool vatSparse = [] {
        const char* value = std::getenv("NSMBW_GX_VAT_SPARSE");
        return value == nullptr || value[0] != '0';
    }();
    const bool nbtOnly =
        vatSparse && descSame && (g_hleGxState.nbtVatDirtyFmt & (1u << fmt)) != 0;
    if (nbtOnly) {
        GxStream::PublishAuroraVtxState(fmt,
                                        GxStream::AuroraVtxPublishOptions{
                                            /*includeNbt=*/false,
                                            /*fmtLoopFirst=*/static_cast<int>(GX_VA_NRM),
                                            /*fmtLoopLast=*/static_cast<int>(GX_VA_NRM)},
                                        false);
    } else {
        GxStream::PublishAuroraVtxState(fmt,
                                        GxStream::AuroraVtxPublishOptions{
                                            /*includeNbt=*/false,
                                            /*fmtLoopFirst=*/static_cast<int>(GX_VA_POS),
                                            /*fmtLoopLast=*/static_cast<int>(GX_VA_TEX7)},
                                        !descSame);
    }
    g_hleGxState.nbtVatDirtyFmt &= static_cast<uint8_t>(~(1u << fmt));
    valid = true;
    lastFmt = fmt;
    lastGeneration = g_hleGxState.vtxStateGeneration;
    AuroraGetVtxFormatRegs(fmt, lastRegs);
}

} // namespace

void ApplyAuroraVtxStateForBegin(GXVtxFmt fmt) {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_GX_FMT_MEMO");
        return value == nullptr || value[0] != '0';
    }();
    if (!enabled || fmt >= GX_MAX_VTXFMT) {
        ApplyAuroraVtxStateForBeginLegacy(fmt);
        return;
    }

    BeginPublishMemo& memo = g_beginPublish[fmt];
    uint32_t regs[6];
    AuroraGetVtxFormatRegs(fmt, regs);
    const uint32_t generation = g_hleGxState.vtxStateGeneration;
    const bool same = memo.valid && memo.generation == generation;
    if (same && std::memcmp(regs, memo.regs, sizeof(regs)) == 0) {
        return;
    }

    static const bool descSkip = [] {
        const char* value = std::getenv("NSMBW_GX_DESC_SKIP");
        return value == nullptr || value[0] != '0';
    }();
    const bool descSame = descSkip && memo.valid && regs[0] == memo.regs[0] &&
                          regs[1] == memo.regs[1] && regs[5] == memo.regs[5];

    // Another writer (the raw-FIFO publish's NBT entry, a direct GXSetVtxAttrFmt, a DL replay)
    // changed aurora's table for this format without going through the memo. Its rows may have
    // come back to the memoized values, so republish the whole row rather than trusting the mirror.
    const bool vatDiverged = memo.valid && (regs[2] != memo.regs[2] || regs[3] != memo.regs[3] ||
                                            regs[4] != memo.regs[4]);
    const auto& row = g_hleGxState.vtxAttrFmt[fmt];
    const bool nbtDirty = (g_hleGxState.nbtVatDirtyFmt & (1u << fmt)) != 0;
    GXVtxAttrFmtList sparse[27];
    size_t sparseCount = 0;
    for (int attr = static_cast<int>(GX_VA_POS); attr <= static_cast<int>(GX_VA_TEX7); ++attr) {
        const GXAttr gxAttr = static_cast<GXAttr>(attr);
        if (IsMatrixIndexAttr(gxAttr)) {
            continue;
        }
        const auto& f = row[attr];
        bool changed = !memo.vatValid || vatDiverged || !SameVatFmt(memo.vat[attr], f);
        if (attr == GX_VA_NRM && nbtDirty) {
            changed = true;
        }
        if (changed) {
            sparse[sparseCount++] = GXVtxAttrFmtList{gxAttr, f.cnt, f.type, f.frac};
        }
    }
    if (sparseCount != 0) {
        sparse[sparseCount] = GXVtxAttrFmtList{GX_VA_NULL, GX_POS_XYZ, GX_F32, 0};
    }
    const bool publishDesc = !descSame;
    if (sparseCount == 0 && !publishDesc) {
        memo.valid = true;
        memo.generation = generation;
        std::memcpy(memo.regs, regs, sizeof(regs));
        g_hleGxState.nbtVatDirtyFmt &= static_cast<uint8_t>(~(1u << fmt));
        return;
    }

    GxStream::PublishAuroraVtxStateSparse(
        fmt, sparseCount != 0 ? sparse : nullptr, publishDesc,
        GxStream::AuroraVtxPublishOptions{/*includeNbt=*/false,
                                          /*fmtLoopFirst=*/static_cast<int>(GX_VA_POS),
                                          /*fmtLoopLast=*/static_cast<int>(GX_VA_TEX7)});
    g_hleGxState.nbtVatDirtyFmt &= static_cast<uint8_t>(~(1u << fmt));
    for (int attr = static_cast<int>(GX_VA_POS); attr <= static_cast<int>(GX_VA_TEX7); ++attr) {
        memo.vat[attr] = row[attr];
    }
    memo.vatValid = true;
    memo.valid = true;
    memo.generation = generation;
    AuroraGetVtxFormatRegs(fmt, memo.regs);

    static const bool verify = [] {
        const char* value = std::getenv("NSMBW_GX_FMT_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    if (verify && sparseCount != 0) {
        uint32_t sparseRegs[6];
        std::memcpy(sparseRegs, memo.regs, sizeof(sparseRegs));
        GxStream::PublishAuroraVtxState(fmt,
                                        GxStream::AuroraVtxPublishOptions{
                                            /*includeNbt=*/false,
                                            /*fmtLoopFirst=*/static_cast<int>(GX_VA_POS),
                                            /*fmtLoopLast=*/static_cast<int>(GX_VA_TEX7)},
                                        false);
        uint32_t fullRegs[6];
        AuroraGetVtxFormatRegs(fmt, fullRegs);
        if (std::memcmp(sparseRegs, fullRegs, sizeof(fullRegs)) != 0) {
            static int reported = 0;
            if (reported < 4) {
                ++reported;
                int nrmEntry = -1;
                for (size_t i = 0; i < sparseCount; ++i) {
                    if (sparse[i].attr == GX_VA_NRM) nrmEntry = static_cast<int>(sparse[i].cnt);
                }
                RT_LOGF(RT_TAG_RUNTIME,
                        "vtx fmt verify mismatch: fmt %d vat %08x/%08x memoNrm %d rowNrm %d "
                        "sparseNrm %d\n",
                        static_cast<int>(fmt), sparseRegs[2], fullRegs[2],
                        static_cast<int>(memo.vat[GX_VA_NRM].cnt),
                        static_cast<int>(row[GX_VA_NRM].cnt), nrmEntry);
            }
        }
        std::memcpy(memo.regs, fullRegs, sizeof(fullRegs));
    }
}
}

// ============================================================================
// Vertex Descriptor
// ============================================================================

extern "C" void GX__ClearVtxDesc_8016dc34() {
    bool changed = false;
    for(int i=0; i<26; ++i){
        changed |= g_hleGxState.vtxDesc[i] != GX_NONE;
        g_hleGxState.vtxDesc[i]=GX_NONE;
    }
    if (changed) g_hleGxState.InvalidateVtxLayoutHash(HleGxState::kVtxLayoutDescRow);
    // GXClearVtxDesc resets descriptors only; array base/stride state persists.
    GXClearVtxDesc();
}
PPC_NATIVE_OVERRIDE_VOID(801c4070, GX__ClearVtxDesc_8016dc34, (), ());

extern "C" void GX__SetVtxDesc_8016d3a4(uint32_t a, uint32_t t) {
    const uint32_t attr = CanonicalVtxAttr(a);
    if(attr>=26||attr==GX_VA_NULL) return;
    const GXAttrType oldType = g_hleGxState.vtxDesc[attr];
    g_hleGxState.vtxDesc[attr]=(GXAttrType)t;
    if (a == GX_VA_NBT) {
        g_hleGxState.vtxDesc[GX_VA_NBT] = GX_NONE;
    }
    if (g_hleGxState.vtxDesc[attr] != oldType) g_hleGxState.InvalidateVtxLayoutHash(HleGxState::kVtxLayoutDescRow);
    if(IsMatrixIndexAttr((GXAttr)attr)) return;
    GXSetVtxDesc((GXAttr)a, (t==GX_INDEX8||t==GX_INDEX16)?GX_DIRECT:(GXAttrType)t);
}
PPC_NATIVE_OVERRIDE_VOID(801c37c0, GX__SetVtxDesc_8016d3a4, (uint32_t a, uint32_t t), (a, t));

extern "C" void GX__SetVtxDescv_8016d608(uint32_t la) {
    if(!la) return; uint32_t p=la;
    while(true){
        uint32_t a=Memory::Read32(p);
        if(a==0xFFu) break;
        GX__SetVtxDesc_8016d3a4(a, Memory::Read32(p+4));
        p+=8;
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c3a30, GX__SetVtxDescv_8016d608, (uint32_t la), (la));

extern "C" void GX__GetVtxDesc_8016d9f0(uint32_t a, uint32_t tp) {
    GXAttrType type = GX_NONE;
    const uint32_t attr = CanonicalVtxAttr(a);
    if (attr < GX_VA_MAX_ATTR && attr != GX_VA_NULL) {
        type = g_hleGxState.vtxDesc[attr];
    }
    if (tp) {
        Memory::Write32(tp, static_cast<uint32_t>(type));
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c3e20, GX__GetVtxDesc_8016d9f0, (uint32_t a, uint32_t tp), (a, tp));

extern "C" void GX__GetVtxDescv_8016dba4(uint32_t la) {
    if (!la) {
        return;
    }

    uint32_t p = la;
    for (uint32_t a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; ++a) {
        Memory::Write32(p, a);
        Memory::Write32(p + 4, static_cast<uint32_t>(g_hleGxState.vtxDesc[a]));
        p += 8;
    }
    Memory::Write32(p, GX_VA_NBT);
    Memory::Write32(p + 4, static_cast<uint32_t>(g_hleGxState.vtxDesc[GX_VA_NRM]));
    p += 8;
    Memory::Write32(p, GX_VA_NULL);
}
PPC_NATIVE_OVERRIDE_VOID(801c3fe0, GX__GetVtxDescv_8016dba4, (uint32_t la), (la));

// ============================================================================
// Vertex Attribute Format
// ============================================================================

extern "C" void GX__SetVtxAttrFmt_8016dc68(uint32_t vf, uint32_t a, uint32_t c, uint32_t t, uint32_t fr) {
    const uint32_t attr = CanonicalVtxAttr(a);
    bool unchanged = false;
    if(vf<8&&attr<26){
        const VtxAttrFmt oldFmt = g_hleGxState.vtxAttrFmt[vf][attr];
        g_hleGxState.vtxAttrFmt[vf][attr].cnt=(GXCompCnt)c;
        g_hleGxState.vtxAttrFmt[vf][attr].type=(GXCompType)t;
        g_hleGxState.vtxAttrFmt[vf][attr].frac=(u8)fr;
        if (a == GX_VA_NBT) {
            g_hleGxState.vtxAttrFmt[vf][GX_VA_NBT] = {};
        }
        const auto& newFmt = g_hleGxState.vtxAttrFmt[vf][attr];
        const bool fmtChanged =
            oldFmt.cnt != newFmt.cnt || oldFmt.type != newFmt.type || oldFmt.frac != newFmt.frac;
        unchanged = a != GX_VA_NBT && !fmtChanged;
        if (fmtChanged) {
            g_hleGxState.InvalidateVtxLayoutHash(HleGxState::VtxLayoutFmtRow(vf));
        }
    }
    if (vf >= GX_MAX_VTXFMT || a < GX_VA_POS || a >= GX_VA_MAX_ATTR) {
        return;
    }
    if (unchanged) {
        return;
    }
    if (a == GX_VA_NBT && vf < GX_MAX_VTXFMT) {
        g_hleGxState.nbtVatDirtyFmt |= static_cast<uint8_t>(1u << vf);
    }
    GXSetVtxAttrFmt((GXVtxFmt)vf, (GXAttr)a, (GXCompCnt)c, (GXCompType)t, (u8)fr);
}
PPC_NATIVE_OVERRIDE_VOID(801c40b0, GX__SetVtxAttrFmt_8016dc68, (uint32_t vf, uint32_t a, uint32_t c, uint32_t t, uint32_t fr), (vf, a, c, t, fr));

extern "C" void GX__SetVtxAttrFmtv_8016de08(uint32_t vf, uint32_t la) {
    if(!la) return; uint32_t p=la;
    while(true){
        uint32_t a=Memory::Read32(p);
        if(a==0xFFu) break;
        GX__SetVtxAttrFmt_8016dc68(vf, a, Memory::Read32(p+4), Memory::Read32(p+8), Memory::Read32(p+12)&0xFFu);
        p+=16;
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c4250, GX__SetVtxAttrFmtv_8016de08, (uint32_t vf, uint32_t la), (vf, la));

extern "C" void GX__GetVtxAttrFmt_8016e04c(uint32_t vf, uint32_t a, uint32_t cp, uint32_t tp, uint32_t fp) {
    VtxAttrFmt fmt{};
    const uint32_t attr = CanonicalVtxAttr(a);
    if (vf < 8 && attr < GX_VA_MAX_ATTR && attr != GX_VA_NULL) {
        fmt = g_hleGxState.vtxAttrFmt[vf][attr];
    }
    if (cp) {
        Memory::Write32(cp, static_cast<uint32_t>(fmt.cnt));
    }
    if (tp) {
        Memory::Write32(tp, static_cast<uint32_t>(fmt.type));
    }
    if (fp) {
        Memory::Write8(fp, fmt.frac);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c44a0, GX__GetVtxAttrFmt_8016e04c, (uint32_t vf, uint32_t a, uint32_t cp, uint32_t tp, uint32_t fp), (vf, a, cp, tp, fp));

extern "C" void GX__GetVtxAttrFmtv_8016e2b8(uint32_t vf, uint32_t la) {
    if (!la) {
        return;
    }

    uint32_t p = la;
    for (uint32_t a = GX_VA_POS; a <= GX_VA_TEX7; ++a) {
        VtxAttrFmt fmt{};
        if (vf < 8) {
            fmt = g_hleGxState.vtxAttrFmt[vf][a];
        }
        Memory::Write32(p, a);
        Memory::Write32(p + 4, static_cast<uint32_t>(fmt.cnt));
        Memory::Write32(p + 8, static_cast<uint32_t>(fmt.type));
        Memory::Write8(p + 12, fmt.frac);
        p += 16;
    }
    Memory::Write32(p, GX_VA_NULL);
}
PPC_NATIVE_OVERRIDE_VOID(801c4700, GX__GetVtxAttrFmtv_8016e2b8, (uint32_t vf, uint32_t la), (vf, la));

// ============================================================================
// Vertex Arrays
// ============================================================================

extern "C" void GX__SetArray_8016e32c(uint32_t a, uint32_t ba, uint32_t str) {
    const uint32_t attr = CanonicalVtxAttr(a);
    if(attr<26){ g_hleGxState.vtxArray[attr].base=ba; g_hleGxState.vtxArray[attr].stride=str; }
}
PPC_NATIVE_OVERRIDE_VOID(801c4780, GX__SetArray_8016e32c, (uint32_t a, uint32_t ba, uint32_t str), (a, ba, str));

// Switch-artifact entry point for the same SDK function; forwards rather than
// repeating the body.
extern "C" void GX__SetArray_8016e1c4(uint32_t a, uint32_t b, uint32_t s) { GX__SetArray_8016e32c(a, b, s); }
#if 0  // NSMBW: MKW switch-artifact alias of GXSetArray (0x8016e1c4); NSMBW's map has no such split
PPC_NATIVE_OVERRIDE_VOID(801c4780, GX__SetArray_8016e1c4, (uint32_t a, uint32_t b, uint32_t s), (a, b, s));
#endif

// ============================================================================
// Texture Coordinate Generation
// ============================================================================

extern "C" void GX__SetNumTexGens_8016e5a4(uint32_t n) {
    GXSetNumTexGens((u8)n);
}
PPC_NATIVE_OVERRIDE_VOID(801c4a20, GX__SetNumTexGens_8016e5a4, (uint32_t n), (n));

extern "C" void GX__SetTexCoordGen2_8016e37c(uint32_t dc, uint32_t f, uint32_t sp, uint32_t m, uint32_t n, uint32_t pm) {
    if (sp >= static_cast<uint32_t>(GX_MAX_TEXGENSRC)) {
        uint32_t pc = 0;
        uint32_t lr = 0;
        if (auto* cpu = TryGetCpuContext()) {
            pc = cpu->pc;
            lr = cpu->lr;
        }
        RT_LOGF(RT_TAG_GX,
                "GXSetTexCoordGen2 invalid src=%u dst=%u type=%u mtx=%u norm=%u post=%u PC=0x%08X LR=0x%08X\n",
                sp, dc, f, m, n, pm, pc, lr);
    }
    GXSetTexCoordGen2((GXTexCoordID)dc, (GXTexGenType)f, (GXTexGenSrc)sp, m, (GXBool)n, pm);
}
PPC_NATIVE_OVERRIDE_VOID(801c47d0, GX__SetTexCoordGen2_8016e37c, (uint32_t dc, uint32_t f, uint32_t sp, uint32_t m, uint32_t n, uint32_t pm), (dc, f, sp, m, n, pm));

extern "C" void GX__EnableTexOffsets_8016f37c(uint32_t coord, uint32_t lineEnable, uint32_t pointEnable) {
    GXEnableTexOffsets(static_cast<GXTexCoordID>(coord),
                       lineEnable ? GX_TRUE : GX_FALSE,
                       pointEnable ? GX_TRUE : GX_FALSE);
}
PPC_NATIVE_OVERRIDE_VOID(801c5820, GX__EnableTexOffsets_8016f37c, (uint32_t coord, uint32_t lineEnable, uint32_t pointEnable), (coord, lineEnable, pointEnable));

extern "C" void GX__SetLineWidth_8016f314(uint32_t width, uint32_t texOffsets) {
    GXSetLineWidth(static_cast<u8>(width), static_cast<GXTexOffset>(texOffsets));
}
PPC_NATIVE_OVERRIDE_VOID(801c57a0, GX__SetLineWidth_8016f314, (uint32_t width, uint32_t texOffsets), (width, texOffsets));

extern "C" void GX__SetPointSize_8016f348(uint32_t pointSize, uint32_t texOffsets) {
    GXSetPointSize(static_cast<u8>(pointSize), static_cast<GXTexOffset>(texOffsets));
}
PPC_NATIVE_OVERRIDE_VOID(801c57e0, GX__SetPointSize_8016f348, (uint32_t pointSize, uint32_t texOffsets), (pointSize, texOffsets));

// ============================================================================
// Begin/End Drawing
// ============================================================================

namespace {
thread_local bool g_inGxBeginOrFifo = false;
thread_local std::chrono::steady_clock::time_point g_nextDeferredTimingPoll{};

void ServiceDeferredTimingDuringGxWork() {
    // Wall-clock driven, so where it lands in the guest's execution depends on host speed; the
    // deterministic mode (det_clock.h) delivers these events only at their virtual time.
    if (g_inGxBeginOrFifo || DetClock::Enabled()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextDeferredTimingPoll) {
        return;
    }
    g_nextDeferredTimingPoll = now + std::chrono::milliseconds(1);

    g_inGxBeginOrFifo = true;
    try {
        if (Fiber::GuestFiberManager::IsInitialized()) {
            // VI callbacks are what advance EGG::AsyncDisplay on NTSC/PAL60;
            // PAL50 uses an OS alarm. Keep both clocks moving while a slow
            // host frame is still being submitted, but defer fiber switches
            // and Aurora work until the native GX call has unwound.
            VI_HLE_ProcessRetracesDeferred(8);
            OS_HLE_ProcessAlarmsDeferred(8);
            // Audio blocks come due every 3 ms. Without a pump in the drawing phase they pile up
            // and are handled back to back at the next idle point, each waiting for the mix worker
            // to finish the previous one. Handled here, the mix runs alongside the drawing.
            Audio_HLE_PollDeferred();
        } else if (TryGetCpuContext() != nullptr) {
            // Same interrupt isolation as the deferred paths above: this GX call
            // is mid-execution of a translated function, so the retrace
            // callbacks must not publish their registers into its context.
            GuestInterruptCallbackContext interrupt;
            VI_HLE_PollRetrace(interrupt.get());
        }
    } catch (...) {
        g_inGxBeginOrFifo = false;
        throw;
    }
    g_inGxBeginOrFifo = false;
}
}

// The same service for the display-list path: 3D models are drawn with display lists and never
// reach GX__Begin, so a frame's whole model pass used to go by without a pump. Called a few
// hundred times a frame; the clock is only read every eighth call.
void GxServiceDeferredTimingFromDisplayList() {
    static thread_local uint32_t calls = 0;
    if ((++calls & 7u) == 0) {
        ServiceDeferredTimingDuringGxWork();
    }
}

extern "C" void GX__Begin_8016f0f0(uint32_t t, uint32_t vf, uint32_t nv) {
    if(IsDisplayListActive()){
        WriteDisplayListData((u8)(t|vf), 1);
        WriteDisplayListData((u16)nv, 2);
        return;
    }
    ServiceDeferredTimingDuringGxWork();
    g_hleGxState.currentVtxFmt=(GXVtxFmt)vf; g_hleGxState.currentPrim=(GXPrimitive)t;
    g_hleGxState.vertsRemaining=nv; g_hleGxState.inBegin=true; g_hleGxState.auroraBeginCalled=false;
    g_hleGxState.fifoReadOffset = 0;
    g_hleGxState.fifoByteCount=0; g_hleGxState.ResetVertex();
    // A raw begin publishes its own table (with GX_VA_NBT) right before submitting the draw, so
    // the immediate-mode publish would only be overwritten.
    const bool raw = HleTryBeginRaw(static_cast<GXPrimitive>(t), static_cast<GXVtxFmt>(vf),
                                    static_cast<uint16_t>(nv));
    static const bool rawSkipImmediate = [] {
        const char* value = std::getenv("NSMBW_GX_RAW_SKIP_IMM");
        return value == nullptr || value[0] != '0';
    }();
    if (!raw || !rawSkipImmediate) {
        ApplyAuroraVtxStateForBegin(static_cast<GXVtxFmt>(vf));
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c5570, GX__Begin_8016f0f0, (uint32_t t, uint32_t vf, uint32_t nv), (t, vf, nv));

extern "C" void GX__End_80044b30() { HleEndRaw(); g_hleGxState.inBegin=false; GXEnd(); }
PPC_NATIVE_OVERRIDE_VOID(802a35b0, GX__End_80044b30, (), ());  // NSMBW out-of-line GXEnd copy #1 (blr)

extern "C" void GX__End_80048c30() { GX__End_80044b30(); }
PPC_NATIVE_OVERRIDE_VOID(802a7c90, GX__End_80048c30, (), ());  // NSMBW out-of-line GXEnd copy #2 (blr)

extern "C" void GX__DrawSphere_80172a30(uint32_t numMajor, uint32_t numMinor) {
    constexpr uint32_t kAttrCount = 26;
    constexpr float kSphereRadius = 1.0f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;

    std::array<GXAttrType, kAttrCount> savedVtxDesc{};
    std::array<VtxAttrFmt, kAttrCount> savedFmt3{};
    for (uint32_t i = 0; i < kAttrCount; ++i) {
        savedVtxDesc[i] = g_hleGxState.vtxDesc[i];
        savedFmt3[i] = g_hleGxState.vtxAttrFmt[GX_VTXFMT3][i];
    }

    const bool hadTex0 = savedVtxDesc[GX_VA_TEX0] != GX_NONE;

    bool drawOpen = false;
    try {
        EnsureAuroraFrameActive();

        for (uint32_t i = 0; i < kAttrCount; ++i) {
            g_hleGxState.vtxDesc[i] = GX_NONE;
        }
        g_hleGxState.InvalidateVtxLayoutHash();
        GXClearVtxDesc();

        GX__SetVtxDesc_8016d3a4(GX_VA_POS, GX_DIRECT);
        GX__SetVtxDesc_8016d3a4(GX_VA_NRM, GX_DIRECT);
        GX__SetVtxAttrFmt_8016dc68(GX_VTXFMT3, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GX__SetVtxAttrFmt_8016dc68(GX_VTXFMT3, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
        if (hadTex0) {
            GX__SetVtxDesc_8016d3a4(GX_VA_TEX0, GX_DIRECT);
            GX__SetVtxAttrFmt_8016dc68(GX_VTXFMT3, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        }

        const float majorStep = kPi / static_cast<float>(numMajor);
        const float minorStep = kTwoPi / static_cast<float>(numMinor);
        const float invRadius = 1.0f / kSphereRadius;

        for (uint32_t major = 0; major < numMajor; ++major) {
            const float major0 = static_cast<float>(major) * majorStep;
            const float major1 = major0 + majorStep;

            const float ring0 = kSphereRadius * std::sin(major0);
            const float ring1 = kSphereRadius * std::sin(major1);
            const float z0 = kSphereRadius * std::cos(major0);
            const float z1 = kSphereRadius * std::cos(major1);

            const uint16_t vertexCount = static_cast<uint16_t>(((numMinor + 1u) * 2u) & 0xFFFEu);
            GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT3, vertexCount);
            drawOpen = true;

            const float nz0 = z0 * invRadius;
            const float nz1 = z1 * invRadius;

            for (uint32_t minor = 0; minor <= numMinor; ++minor) {
                const float minorAngle = static_cast<float>(minor) * minorStep;
                const float c = std::cos(minorAngle);
                const float s = std::sin(minorAngle);

                const float x1 = c * ring1;
                const float y1 = s * ring1;
                GXPosition3f32(x1, y1, z1);
                GXNormal3f32(x1 * invRadius, y1 * invRadius, nz1);
                if (hadTex0) {
                    GXTexCoord2f32(static_cast<float>(minor) / static_cast<float>(numMinor),
                                   static_cast<float>(major + 1u) / static_cast<float>(numMajor));
                }

                const float x0 = c * ring0;
                const float y0 = s * ring0;
                GXPosition3f32(x0, y0, z0);
                GXNormal3f32(x0 * invRadius, y0 * invRadius, nz0);
                if (hadTex0) {
                    GXTexCoord2f32(static_cast<float>(minor) / static_cast<float>(numMinor),
                                   static_cast<float>(major) / static_cast<float>(numMajor));
                }
            }
            GXEnd();
            drawOpen = false;
        }
    } catch (...) {
        if (drawOpen) {
            GXEnd();
            drawOpen = false;
        }
    }

    for (uint32_t i = 0; i < kAttrCount; ++i) {
        g_hleGxState.vtxDesc[i] = GX_NONE;
    }
    g_hleGxState.InvalidateVtxLayoutHash();
    GXClearVtxDesc();
    for (uint32_t attr = 0; attr < kAttrCount; ++attr) {
        const GXAttrType type = savedVtxDesc[attr];
        if (type != GX_NONE) {
            GX__SetVtxDesc_8016d3a4(attr, static_cast<uint32_t>(type));
        }
    }
    for (uint32_t attr = GX_VA_POS; attr <= GX_VA_TEX7; ++attr) {
        const VtxAttrFmt& fmt = savedFmt3[attr];
        GX__SetVtxAttrFmt_8016dc68(GX_VTXFMT3, attr, static_cast<uint32_t>(fmt.cnt), static_cast<uint32_t>(fmt.type), fmt.frac);
    }
}
PPC_NATIVE_OVERRIDE_VOID(801c90a0, GX__DrawSphere_80172a30, (uint32_t numMajor, uint32_t numMinor), (numMajor, numMinor));
