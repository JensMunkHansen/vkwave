#pragma once

#include <vkwave/pipeline/frame_resource_pool.h>
#include <vkwave/pipeline/imgui_overlay.h>

#include <vulkan/vulkan.hpp>

#include <memory>

struct Engine;
struct SceneData;
namespace vkwave { class ExecutionGroup; class Swapchain; class Buffer; }

/// Pipeline infrastructure: render passes, sampler, execution group wiring,
/// ImGui, MSAA. The HDR render target is owned by the render graph's resource
/// pool (referenced here by handle). References SceneData for descriptor writes.
struct ScenePipeline
{
  static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;

  // Graph-owned HDR color target (one per slot), referenced by handle.
  vkwave::FrameResourcePool::ColorHandle hdr_handle{ 0 };
  vk::Sampler hdr_sampler{ VK_NULL_HANDLE };
  vk::RenderPass scene_renderpass{ VK_NULL_HANDLE };
  vk::RenderPass composite_renderpass{ VK_NULL_HANDLE };
  vk::SampleCountFlagBits msaa_samples{ vk::SampleCountFlagBits::e1 };
  std::unique_ptr<vkwave::ImGuiOverlay> imgui;

  ScenePipeline(Engine& engine, SceneData& data, vk::SampleCountFlagBits msaa);
  ~ScenePipeline();

  ScenePipeline(const ScenePipeline&) = delete;
  ScenePipeline& operator=(const ScenePipeline&) = delete;

  /// Replace the offscreen group with new MSAA settings.
  /// Does NOT rewire record callbacks -- caller must do that.
  void rebuild_for_msaa(vk::SampleCountFlagBits new_samples, SceneData& data);

  /// Recreate size-dependent resources after swapchain resize.
  void resize(const vkwave::Swapchain& swapchain, SceneData& data);

  /// Write per-material + IBL texture descriptors to the PBR group.
  void write_pbr_descriptors(SceneData& data);

  /// Destroy and recreate PBR group frame resources, then rewrite descriptors.
  void rebuild_pbr_descriptors(SceneData& data);

  /// Write IBL descriptors only (set 2).
  void write_ibl_descriptors(SceneData& data);

  vkwave::ExecutionGroup& pbr_group();
  vkwave::ExecutionGroup& composite_group();
  vkwave::ImGuiOverlay* imgui_overlay() { return imgui.get(); }

private:
  Engine* m_engine;

  // Immutable per-material constants (GpuMaterial[]), shared across all frames.
  // Built once per model load; only the descriptor is rewritten on rebuild.
  std::unique_ptr<vkwave::Buffer> material_buffer;

  /// (Re)build the material SSBO from the active materials and write its
  /// descriptor to set 2. Called from write_pbr_descriptors().
  void upload_material_buffer(SceneData& data);
};
