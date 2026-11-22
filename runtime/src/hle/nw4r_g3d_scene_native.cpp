// Native nw4r::g3d ScnGroup traversal.
//
// ScnGroup::DefG3dProcScnGroup is the largest single Newer guest function. The translated body
// writes about 17 guest registers plus CR/CTR/XER into the context at every child call and reads
// them back. These natives keep the loop state in host variables and pass only the four argument
// registers each child reads. The callbacks, the world/view matrix work and the child dispatch
// keep the guest semantics of the translated bodies:
//
//   CALC_WORLD  callback A, virtual CalcWorldMtx, callback B, children with the world matrix,
//               callback C
//   CALC_MAT    callback A, children, callback C
//   CALC_VIEW   callback A, view = camera * world, callback B, children, callback C
//   GATHER      ScnGroup_G3DPROC_GATHER_SCNOBJ
//   DRAW_*      nothing
//   CALC_VTX / UPDATEFRAME / task 0   children with the same task
//
// Attach/detach and every other task keep the translated function. NSMBW_G3D_SCENE_NATIVE=0
// forces the translated bodies for A/B.

#include "abi_bridge.h"
#include "memory.h"
#include "ppc_runtime.h"

#include <cstdint>
#include <cstdlib>

extern "C" void func_80257E60(CpuContext* ctx);  // ScnGroup_G3DPROC_GATHER_SCNOBJ
extern "C" void ScnGroupDefG3dProcTranslated(CpuContext* ctx);
extern "C" void ScnRootG3dProcTranslated(CpuContext* ctx);
extern "C" void ScnRootCalcMaterialTranslated(CpuContext* ctx);

#define MKW_STATIC_TRANSLATED_CALL(Target, Entry, Context) \
    do { ApplyRuntimeCallOptions(Target, Context); Entry(Context); } while (false)
#include "nw4r_g3d_scngroup_translated.inc"
#include "nw4r_g3d_scnroot_translated.inc"
#include "nw4r_g3d_scnroot_calcmat_translated.inc"
#undef MKW_STATIC_TRANSLATED_CALL

namespace {

// ScnObj layout (nw4r/g3d/g3d_scnobj.h): the matrix array at 0x0C is MTX_LOCAL, MTX_WORLD at
// 0x3C and MTX_VIEW at 0x6C; the callback interface and its masks follow the bounds.
constexpr uint32_t kScnObjFlags = 0xCC;
constexpr uint32_t kMtxWorld = 0x3C;
constexpr uint32_t kMtxView = 0x6C;
constexpr uint32_t kFuncObjExec = 0xD4;
constexpr uint32_t kCallbackTiming = 0xD8;
constexpr uint32_t kCallbackExecOpMask = 0xDA;
constexpr uint32_t kScnGroupArray = 0xDC;
constexpr uint32_t kScnGroupCount = 0xE4;

constexpr uint32_t kVtableG3dProc = 0x0C;
constexpr uint32_t kVtableCalcWorldMtx = 0x30;
constexpr uint32_t kVtableExecCalcWorld = 0x0C;
constexpr uint32_t kVtableExecCalcMat = 0x10;
constexpr uint32_t kVtableExecCalcView = 0x14;

constexpr uint32_t kScnGroupG3dProc = 0x80257F80u;
constexpr uint32_t kScnGroupDefG3dProc = 0x80257FC0u;
constexpr uint32_t kScnGroupGather = 0x80257E60u;
constexpr uint32_t kPSMTXConcat = 0x801C0540u;

constexpr uint32_t kTaskCalcWorld = 1;
constexpr uint32_t kTaskCalcMat = 2;
constexpr uint32_t kTaskCalcView = 4;
constexpr uint32_t kTaskGather = 5;
constexpr uint32_t kTaskDrawOpa = 6;
constexpr uint32_t kTaskDrawXlu = 7;
constexpr uint32_t kTaskUpdateFrame = 8;

constexpr uint32_t kExecOpCalcWorld = 1u << 0;
constexpr uint32_t kExecOpCalcMat = 1u << 1;
constexpr uint32_t kExecOpCalcView = 1u << 2;
constexpr uint32_t kTimingA = 1u << 0;
constexpr uint32_t kTimingB = 1u << 1;
constexpr uint32_t kTimingC = 1u << 2;

bool NativeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NSMBW_G3D_SCENE_NATIVE");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

bool ScnObjProcDisabled(uint32_t self, uint32_t task) {
    if (task == 0 || task >= 9) {
        return false;
    }
    return (MemoryInline::FlatRead32(self + kScnObjFlags) & (1u << (task - 1))) != 0;
}

void ExecCallback(CpuContext* ctx, uint32_t self, uint32_t execOp, uint32_t timing,
                  uint32_t vtableOffset, uint32_t param, uint32_t info) {
    const uint32_t callback = MemoryInline::FlatRead32(self + kFuncObjExec);
    if (callback == 0) {
        return;
    }
    if ((MemoryInline::FlatRead16(self + kCallbackExecOpMask) & execOp) == 0) {
        return;
    }
    if ((MemoryInline::FlatRead8(self + kCallbackTiming) & timing) == 0) {
        return;
    }
    ctx->gpr[3] = callback;
    ctx->gpr[4] = timing;
    ctx->gpr[5] = self;
    ctx->gpr[6] = param;
    ctx->gpr[7] = info;
    InvokeIndirectCpu(MemoryInline::FlatRead32(MemoryInline::FlatRead32(callback) + vtableOffset), ctx);
}

} // namespace

extern "C" void NW4R_G3D_ScnGroupDefG3dProc_HLE(CpuContext* ctx);

namespace {

// The translated loop reloads the count and the array every iteration, in case a child changes
// the group while it runs.
void LoopChildren(CpuContext* ctx, uint32_t self, uint32_t task, uint32_t param, uint32_t info) {
    for (uint32_t i = 0; i < MemoryInline::FlatRead32(self + kScnGroupCount); ++i) {
        const uint32_t array = MemoryInline::FlatRead32(self + kScnGroupArray);
        const uint32_t child = MemoryInline::FlatRead32(array + i * 4u);
        const uint32_t target =
            MemoryInline::FlatRead32(MemoryInline::FlatRead32(child) + kVtableG3dProc);
        ctx->gpr[3] = child;
        ctx->gpr[4] = task;
        ctx->gpr[5] = param;
        ctx->gpr[6] = info;
        if (target == kScnGroupG3dProc) {
            NW4R_G3D_ScnGroupG3dProc_HLE(ctx);
        } else if (target == kScnGroupDefG3dProc) {
            NW4R_G3D_ScnGroupDefG3dProc_HLE(ctx);
        } else {
            InvokeIndirectCpu(target, ctx);
        }
    }
}

} // namespace

extern "C" void NW4R_G3D_ScnGroupDefG3dProc_HLE(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const uint32_t task = ctx->gpr[4];
    const uint32_t param = ctx->gpr[5];
    const uint32_t info = ctx->gpr[6];

    if (!NativeEnabled() || self == 0) {
        ScnGroupDefG3dProcTranslated(ctx);
        return;
    }

    switch (task) {
    case kTaskCalcWorld: {
        ExecCallback(ctx, self, kExecOpCalcWorld, kTimingA, kVtableExecCalcWorld, param, info);

        // CalcWorldMtx(parent, &param) may scale param; the scaled value goes to the callbacks and
        // the children. The scratch slot lives in the guest stack frame, like the translated body.
        const uint32_t savedSp = ctx->gpr[1];
        const uint32_t frame = savedSp - 32u;
        MemoryInline::FlatWriteRam32(frame + 8u, param);
        ctx->gpr[1] = frame;
        ctx->gpr[3] = self;
        ctx->gpr[4] = info;
        ctx->gpr[5] = frame + 8u;
        InvokeIndirectCpu(
            MemoryInline::FlatRead32(MemoryInline::FlatRead32(self) + kVtableCalcWorldMtx), ctx);
        const uint32_t worldParam = MemoryInline::FlatRead32(frame + 8u);
        ctx->gpr[1] = savedSp;

        ExecCallback(ctx, self, kExecOpCalcWorld, kTimingB, kVtableExecCalcWorld, worldParam, info);
        LoopChildren(ctx, self, kTaskCalcWorld, worldParam, self + kMtxWorld);
        ExecCallback(ctx, self, kExecOpCalcWorld, kTimingC, kVtableExecCalcWorld, worldParam, info);
        return;
    }

    case kTaskCalcMat:
        ExecCallback(ctx, self, kExecOpCalcMat, kTimingA, kVtableExecCalcMat, param, info);
        LoopChildren(ctx, self, kTaskCalcMat, param, info);
        ExecCallback(ctx, self, kExecOpCalcMat, kTimingC, kVtableExecCalcMat, param, info);
        return;

    case kTaskCalcView: {
        ExecCallback(ctx, self, kExecOpCalcView, kTimingA, kVtableExecCalcView, param, info);

        ctx->gpr[3] = info;
        ctx->gpr[4] = self + kMtxWorld;
        ctx->gpr[5] = self + kMtxView;
        InvokeDirectCpu<kPSMTXConcat>(ctx);

        ExecCallback(ctx, self, kExecOpCalcView, kTimingB, kVtableExecCalcView, param, info);
        LoopChildren(ctx, self, kTaskCalcView, param, info);
        ExecCallback(ctx, self, kExecOpCalcView, kTimingC, kVtableExecCalcView, param, info);
        return;
    }

    case kTaskGather:
        ctx->gpr[3] = self;
        ctx->gpr[4] = param;
        ctx->gpr[5] = info;
        InvokeDirectCpu<kScnGroupGather>(ctx);
        return;

    case kTaskDrawOpa:
    case kTaskDrawXlu:
        return;

    case 0x10001: {  // CHILD_DETACHED: the group removes the child through its vtable slot
        ctx->gpr[3] = self;
        ctx->gpr[4] = info;
        InvokeIndirectCpu(MemoryInline::FlatRead32(MemoryInline::FlatRead32(self) + 0x3C), ctx);
        return;
    }

    case 0x10002:  // ATTACH_PARENT
        MemoryInline::FlatWrite32(self + 4, info);
        return;

    case 0x10003:  // DETACH_PARENT, plus the unused 0x10004/0x10005 slots
    case 0x10004:
    case 0x10005:
        MemoryInline::FlatWrite32(self + 4, 0);
        return;

    default:
        // Task 0, CALC_VTX, UPDATEFRAME, tasks 9..0x10000 and >= 0x10006 loop with the same task.
        LoopChildren(ctx, self, task, param, info);
        return;
    }
}

extern "C" void NW4R_G3D_ScnGroupG3dProc_HLE(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    if (self == 0) {
        return;
    }
    if (ScnObjProcDisabled(self, ctx->gpr[4])) {
        return;
    }
    NW4R_G3D_ScnGroupDefG3dProc_HLE(ctx);
}

PPC_NATIVE_OVERRIDE_VOID(80257fc0, NW4R_G3D_ScnGroupDefG3dProc_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(80257f80, NW4R_G3D_ScnGroupG3dProc_HLE, (CpuContext* ctx), (ctx));

namespace {

constexpr uint32_t kScnRootChildDetach = 10376;  // the child the root currently detaches
constexpr uint32_t kTaskChildDetached = 0x10001;
constexpr uint32_t kTaskDetachParent = 0x10003;

} // namespace

// ScnRoot::G3dProc forwards every task to ScnGroup::DefG3dProcScnGroup after the disabled check,
// except CHILD_DETACHED, which the root claims when the detached child is the one it tracks.
extern "C" void NW4R_G3D_ScnRootG3dProc_HLE(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    const uint32_t task = ctx->gpr[4];
    const uint32_t info = ctx->gpr[6];

    if (self != 0 && NativeEnabled() && !ScnObjProcDisabled(self, task)) {
        if (task == kTaskChildDetached) {
            const uint32_t detached = MemoryInline::FlatRead32(self + kScnRootChildDetach);
            if (detached == info) {
                ctx->gpr[3] = detached;
                ctx->gpr[4] = kTaskDetachParent;
                ctx->gpr[5] = 0;
                ctx->gpr[6] = self;
                InvokeIndirectCpu(
                    MemoryInline::FlatRead32(MemoryInline::FlatRead32(detached) + kVtableG3dProc), ctx);
                MemoryInline::FlatWrite32(self + kScnRootChildDetach, 0);
                return;
            }
        }
        NW4R_G3D_ScnGroupDefG3dProc_HLE(ctx);
        return;
    }
    ScnRootG3dProcTranslated(ctx);
}

// ScnRoot::CalcMaterial is a tail dispatch of CALC_MAT with param and info zero.
extern "C" void NW4R_G3D_ScnRootCalcMaterial_HLE(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    if (self == 0 || !NativeEnabled()) {
        ScnRootCalcMaterialTranslated(ctx);
        return;
    }
    ctx->gpr[4] = kTaskCalcMat;
    ctx->gpr[5] = 0;
    ctx->gpr[6] = 0;
    InvokeIndirectJump(MemoryInline::FlatRead32(MemoryInline::FlatRead32(self) + kVtableG3dProc), ctx);
}

PPC_NATIVE_OVERRIDE_VOID(80258b20, NW4R_G3D_ScnRootG3dProc_HLE, (CpuContext* ctx), (ctx));
PPC_NATIVE_OVERRIDE_VOID(802590f0, NW4R_G3D_ScnRootCalcMaterial_HLE, (CpuContext* ctx), (ctx));
