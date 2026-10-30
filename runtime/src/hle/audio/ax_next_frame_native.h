#pragma once

#include "isa/ppc_isa_context.h"

namespace AxNextFrameNative {

// True when NSMBW_AX_NEXTFRAME is native/on/1.
bool Enabled();
// Writes the next command list for stereo output. Returns false when the mode is
// not stereo, in which case the caller must run the translated original.
bool Run(CpuContext* ctx);
// Runs translated and native on the same state and logs command-list differences.
// The translated output is left in place.
void Compare(CpuContext* ctx, void (*translated)(CpuContext*));

}  // namespace AxNextFrameNative
