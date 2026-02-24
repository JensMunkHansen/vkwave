#pragma once

#include <vkwave/core/image.h>
#include <vkwave/pipeline/imgui_overlay.h>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

struct Engine;
struct SceneData;
namespace vkwave { class ExecutionGroup; class Swapchain; }

/// Pipeline infrastructure: render passes, HDR images, sampler, execution
/// group wiring, ImGui, MSAA. References SceneData for descriptor writes.
struct ScenePipeline
{
  static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;

  std::vector<vkwave::Image> hdr_images;
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

  void create_hdr_images(vk::Extent2D extent, uint32_t count);
};
