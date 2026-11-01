// Two more nw4r::g3d leaves that the per-node path hits every frame: the resource node
// lookup the bytecode walk uses, and the texture-pattern override selector.
//
// 0x8023B300 ResMdl::GetResNode(u32)
// 0x80245440 AnmObjTexPatOverride::GetResult(TexPatAnmResult*, u32)
#include "hle_stubs.h"

#include "memory.h"
#include "runtime_log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

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

extern "C" void NW4R_G3D_GetResNode_Translated(CpuContext* ctx);
extern "C" void NW4R_G3D_TexPatGetResult_Translated(CpuContext* ctx);

#include "nw4r_g3d_resnode_translated.inc"
#include "nw4r_g3d_texpat_translated.inc"

extern "C" void NW4R_G3D_GetResNode_HLE(CpuContext* ctx) {
    if (!NativeEnabled()) {
        NW4R_G3D_GetResNode_Translated(ctx);
        return;
    }
    const uint32_t mdl = ctx->gpr[3];
    const uint32_t id = ctx->gpr[4];
    uint32_t result = 0;
    const uint32_t data = MemoryInline::FlatRead32(mdl);
    uint32_t arrayOffset = MemoryInline::FlatRead32(data + 20);
    if (arrayOffset != 0) {
        const uint32_t array = data + arrayOffset;
        const uint32_t entry = MemoryInline::FlatRead32(array + (id + 1u) * 16u + 20u);
        result = entry != 0 ? array + entry : 0;
    }

    if (CompareEnabled()) {
        SavedRegisters entry;
        SaveRegisters(ctx, entry);
        NW4R_G3D_GetResNode_Translated(ctx);
        const uint32_t reference = ctx->gpr[3];
        LoadRegisters(ctx, entry);
        static uint32_t compares = 0;
        static uint32_t mismatches = 0;
        ++compares;
        if (reference != result && mismatches++ == 0) {
            RT_LOGF(RT_TAG_HLE, "g3d resnode compare mismatch: mdl %08X id %u native %08X translated %08X\n",
                    mdl, id, result, reference);
        }
        if (compares % 100000u == 0) {
            RT_LOGF(RT_TAG_HLE, "g3d resnode compare: %u calls, %u mismatches\n", compares,
                    mismatches);
        }
        ctx->gpr[3] = reference;
        return;
    }
    ctx->gpr[3] = result;
}

PPC_NATIVE_OVERRIDE_VOID(8023B300, NW4R_G3D_GetResNode_HLE, (CpuContext* ctx), (ctx));

extern "C" void NW4R_G3D_TexPatGetResult_HLE(CpuContext* ctx) {
    if (!NativeEnabled()) {
        NW4R_G3D_TexPatGetResult_Translated(ctx);
        return;
    }
    const uint32_t self = ctx->gpr[3];
    const uint32_t resultPtr = ctx->gpr[4];
    const uint32_t id = ctx->gpr[5];
    SavedRegisters entry;
    SaveRegisters(ctx, entry);

    uint32_t result = resultPtr;
    bool found = false;
    const int32_t count = static_cast<int32_t>(MemoryInline::FlatRead32(self + 24));
    const uint32_t array = MemoryInline::FlatRead32(self + 28);
    for (int32_t i = count - 1; i >= 0; --i) {
        const uint32_t child = MemoryInline::FlatRead32(array + static_cast<uint32_t>(i) * 4u);
        if (child == 0) {
            continue;
        }
        const uint32_t binding = MemoryInline::FlatRead32(child + 20) + id * 2u;
        if ((MemoryInline::FlatRead16(binding) & 0xC000u) != 0) {
            continue;
        }
        const uint32_t vtable = MemoryInline::FlatRead32(child);
        const uint32_t entry = MemoryInline::FlatRead32(vtable + 56);
        ctx->gpr[3] = child;
        ctx->gpr[4] = resultPtr;
        ctx->gpr[5] = id;
        ctx->gpr[12] = entry;
        ctx->ctr = entry;
        ctx->lr = 0x802454ACu;
        InvokeIndirectCpu(entry, ctx);
        result = ctx->gpr[3];
        if (MemoryInline::FlatRead8(result) != 0 || MemoryInline::FlatRead8(result + 1) != 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        // No child provided the id (or every child's result was empty): the translated body
        // clears the two existence bytes and returns the caller's buffer.
        MemoryInline::FlatWrite8(resultPtr, 0);
        MemoryInline::FlatWrite8(resultPtr + 1, 0);
        result = resultPtr;
    }

    if (CompareEnabled()) {
        LoadRegisters(ctx, entry);
        NW4R_G3D_TexPatGetResult_Translated(ctx);
        const uint32_t reference = ctx->gpr[3];
        const uint8_t refTex = MemoryInline::FlatRead8(resultPtr);
        const uint8_t refPltt = MemoryInline::FlatRead8(resultPtr + 1);
        LoadRegisters(ctx, entry);
        static uint32_t compares = 0;
        static uint32_t mismatches = 0;
        ++compares;
        const bool mismatch = reference != result || refTex != MemoryInline::FlatRead8(resultPtr) ||
                              refPltt != MemoryInline::FlatRead8(resultPtr + 1);
        if (mismatch && mismatches++ == 0) {
            RT_LOGF(RT_TAG_HLE, "g3d texpat compare mismatch: self %08X id %u native %08X translated %08X\n",
                    self, id, result, reference);
        }
        if (compares % 100000u == 0) {
            RT_LOGF(RT_TAG_HLE, "g3d texpat compare: %u calls, %u mismatches\n", compares,
                    mismatches);
        }
        ctx->gpr[3] = reference;
        return;
    }
    ctx->gpr[3] = result;
}

PPC_NATIVE_OVERRIDE_VOID(80245440, NW4R_G3D_TexPatGetResult_HLE, (CpuContext* ctx), (ctx));
