#pragma once

// Hooks the Switch runtime provides to aurora. Declared in one place so the aurora sources share
// one contract; the runtime defines them in platform/switch/aurora_hooks.cpp.
#if defined(MKW_PLATFORM_SWITCH)
extern "C" {
// Pins the GX decode worker to its own core at the guest's priority.
void MkwConfigureGxWorkerThread();
// Chooses the frame worker's preferred core and the cores it may migrate to.
void MkwConfigureFrameWorkerThread();
// level 0 idle, 1 background prewarm, 2 boosted prewarm.
void MkwSetPipelineWorkerPriority(int level);
}
#endif
