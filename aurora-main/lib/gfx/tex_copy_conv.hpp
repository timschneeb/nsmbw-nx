#pragma once

#include "common.hpp"

#include <dolphin/gx/GXEnum.h>

namespace aurora::gfx::tex_copy_conv {

enum class SampleFilter : uint8_t {
  Nearest,
  Linear,
};

struct ConvRequest {
  GXTexFmt fmt;
  wgpu::TextureView srcView; // View of resolved EFB / offscreen color/depth
  Range uniformRange;        // UV transform uniform (offset + scale)
  TextureHandle dst;         // Destination texture
  SampleFilter sampleFilter = SampleFilter::Nearest;
  bool forceOpaqueAlpha = false;
};

bool needs_conversion(GXTexFmt fmt);

void initialize();
void shutdown();
void run(const wgpu::CommandEncoder& cmd, const ConvRequest& req);
void blit(const wgpu::CommandEncoder& cmd, const ConvRequest& req);

// NSMBW: one render pass into `grid` for many copies, each clipped to its own cell. Every cell
// must have the same size and the grid must carry the format the pipeline writes. The caller
// copies the cells out afterwards; this exists so 60 tile copies cost one pass instead of 60.
struct CellCopy {
  ConvRequest req;
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};
void run_cells(const wgpu::CommandEncoder& cmd, const TextureHandle& grid, bool conversion,
               const CellCopy* cells, size_t count);

} // namespace aurora::gfx::tex_copy_conv
