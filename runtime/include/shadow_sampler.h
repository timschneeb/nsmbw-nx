#pragma once

#include <cstdint>
#include <filesystem>

// Switch shadow sampler: translated function entry and exit update a shadow PC for the
// guest host thread, and a 1 kHz thread pinned away from the guest core samples it.
// Only compiled when MKW_ENABLE_SHADOW_SAMPLER is on.
void NsmbwShadowSamplerInit(const std::filesystem::path& logDirectory);
void NsmbwShadowSamplerSetScene(uint32_t scene);
void NsmbwShadowSamplerFrame(uint64_t frame);
