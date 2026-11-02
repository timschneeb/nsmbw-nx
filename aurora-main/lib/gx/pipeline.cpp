#include "pipeline.hpp"

#include "../webgpu/gpu.hpp"
#include "gx_fmt.hpp"
#include "shader_info.hpp"
#include "tracy/Tracy.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <absl/container/flat_hash_map.h>

namespace aurora::gx {
static Module Log("aurora::gx");

namespace {
struct ShaderConfigHash {
  size_t operator()(const ShaderConfig& config) const noexcept { return static_cast<size_t>(xxh3_hash(config)); }
};

struct CachedShaderModule {
  std::condition_variable ready;
  wgpu::ShaderModule module;
  bool compiling = true;
};

std::mutex sShaderModuleCacheMutex;
absl::flat_hash_map<ShaderConfig, std::shared_ptr<CachedShaderModule>, ShaderConfigHash> sShaderModuleCache;

// Prewarm attribution: Tint (CreateShaderModule on a cache miss) versus the backend pipeline
// object. Logged at the end of the boot prewarm.
std::atomic<uint64_t> sShaderBuildNanos{0};
std::atomic<uint64_t> sShaderBuildCount{0};
std::atomic<uint64_t> sPipelineBuildNanos{0};
std::atomic<uint64_t> sPipelineBuildCount{0};

wgpu::ShaderModule cached_shader_module(const ShaderConfig& config) {
  std::shared_ptr<CachedShaderModule> entry;
  {
    std::unique_lock lock{sShaderModuleCacheMutex};
    const auto it = sShaderModuleCache.find(config);
    if (it == sShaderModuleCache.end()) {
      entry = std::make_shared<CachedShaderModule>();
      sShaderModuleCache.emplace(config, entry);
    } else {
      entry = it->second;
      entry->ready.wait(lock, [&] { return !entry->compiling; });
      return entry->module;
    }
  }

  const auto shaderStart = std::chrono::steady_clock::now();
  auto module = build_shader(config);
  sShaderBuildNanos.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         std::chrono::steady_clock::now() - shaderStart)
                                                         .count()),
                              std::memory_order_relaxed);
  sShaderBuildCount.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard lock{sShaderModuleCacheMutex};
    entry->module = module;
    entry->compiling = false;
  }
  entry->ready.notify_all();
  return module;
}
} // namespace

wgpu::RenderPipeline create_pipeline(const PipelineConfig& config) {
  ZoneScoped;
  const auto shader = cached_shader_module(config.shaderConfig);
  const auto pipelineStart = std::chrono::steady_clock::now();
  auto pipeline = build_pipeline(config, {}, shader, "GX Pipeline");
  sPipelineBuildNanos.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                           std::chrono::steady_clock::now() - pipelineStart)
                                                           .count()),
                                std::memory_order_relaxed);
  sPipelineBuildCount.fetch_add(1, std::memory_order_relaxed);
  return pipeline;
}

void pipeline_build_stats(uint64_t& shaderNanos, uint64_t& shaderCount, uint64_t& pipelineNanos,
                          uint64_t& pipelineCount) {
  shaderNanos = sShaderBuildNanos.load(std::memory_order_relaxed);
  shaderCount = sShaderBuildCount.load(std::memory_order_relaxed);
  pipelineNanos = sPipelineBuildNanos.load(std::memory_order_relaxed);
  pipelineCount = sPipelineBuildCount.load(std::memory_order_relaxed);
}

void reset_pipeline_build_stats() {
  sShaderBuildNanos.store(0, std::memory_order_relaxed);
  sShaderBuildCount.store(0, std::memory_order_relaxed);
  sPipelineBuildNanos.store(0, std::memory_order_relaxed);
  sPipelineBuildCount.store(0, std::memory_order_relaxed);
}

void clear_shader_module_cache() {
  std::lock_guard lock{sShaderModuleCacheMutex};
  sShaderModuleCache.clear();
}

void render(const DrawData& data, const wgpu::RenderPassEncoder& pass, DrawEncodeState& state,
            bool requireReadyPipeline, const gfx::Range* uniformRangeOverride) {
  if (!gfx::bind_pipeline(data.pipeline, pass, state.currentPipeline, requireReadyPipeline)) {
    return;
  }

  // An interpolated presentation slot re-encodes the identical draw with only this range replaced; overriding here avoids copying the whole DrawData per draw per slot.
  const gfx::Range& uniformRange = uniformRangeOverride != nullptr ? *uniformRangeOverride : data.uniformRange;
  const std::array offsets{uniformRange.offset};
  pass.SetBindGroup(1, gfx::g_uniformBindGroup, offsets.size(), offsets.data());
  // Resolved when the draw was recorded, or made here the first time; see GXBindGroups.
  WGPUBindGroup textureBindGroup = data.bindGroups.resolvedTextureBindGroup;
  if (textureBindGroup == nullptr && data.bindGroups.textureBindGroup != 0) {
    textureBindGroup = resolve_texture_bind_group(data.bindGroups.textureBindGroup);
  }
  if (textureBindGroup != nullptr && textureBindGroup != state.boundTextureBindGroup) {
    wgpuRenderPassEncoderSetBindGroup(pass.Get(), 2, textureBindGroup, 0, nullptr);
    state.boundTextureBindGroup = textureBindGroup;
  }
  if (data.dstAlpha != UINT32_MAX) {
    const wgpu::Color color{0.f, 0.f, 0.f, data.dstAlpha / 255.f};
    pass.SetBlendConstant(&color);
  }
  if (!state.indexBufferBound) {
    // Bound once for the pass; draws select their range with firstIndex below.
    pass.SetIndexBuffer(gfx::g_indexBuffer, wgpu::IndexFormat::Uint16, 0, wgpu::kWholeSize);
    state.indexBufferBound = true;
  }
  pass.DrawIndexed(data.indexCount, data.instanceCount,
                   static_cast<uint32_t>(data.idxRange.offset / sizeof(uint16_t)));
}
} // namespace aurora::gx
