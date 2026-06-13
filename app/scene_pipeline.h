#pragma once

#include <vkwave/pipeline/frame_resource_pool.h>
#include <vkwave/pipeline/imgui_overlay.h>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <optional>

struct Engine;
struct SceneData;
namespace vkwave { class ExecutionGroup; class Swapchain; class Buffer; }

/// Pipeline infrastructure: render passes, sampler, execution group wiring,
/// ImGui, MSAA. The HDR render target is owned by the render graph's resource
/// pool (referenced here by handle). References SceneData for descriptor writes.
struct ScenePipeline
{
  static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;

  // Graph-owned HDR color target + depth (one per slot), referenced by handle.
  vkwave::FrameResourcePool::ColorHandle hdr_handle{ 0 };
  vkwave::FrameResourcePool::DepthHandle depth_handle{ 0 };
  // Per-slot snapshot of the opaque HDR, sampled by the transmission pass for
  // refraction. Registered only when the scene has transmissive materials
  // (engaged == has value); otherwise the graph is identical to opaque-only.
  std::optional<vkwave::FrameResourcePool::ColorHandle> snapshot_handle;
  vk::Sampler hdr_sampler{ VK_NULL_HANDLE };
  vk::RenderPass scene_renderpass{ VK_NULL_HANDLE };
  vk::RenderPass composite_renderpass{ VK_NULL_HANDLE };
  vk::RenderPass transmission_renderpass{ VK_NULL_HANDLE };
  static constexpr vk::Format kDepthFormat = vk::Format::eD32Sfloat;
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

  /// Structurally rebuild the graph for the current scene — adds/removes the
  /// transmission pass + snapshot resource depending on data.has_transmission().
  /// Call when the *pass set* changes (model switch crossing the glass boundary,
  /// or MSAA change). Drains internally. Caller must re-wire record callbacks
  /// afterwards (the group objects are new).
  void rebuild_graph(SceneData& data);

  /// True if the current graph includes the transmission pass.
  [[nodiscard]] bool has_transmission_pass() const { return m_graph_has_transmission; }

  /// Write per-material + IBL texture descriptors to the PBR group.
  void write_pbr_descriptors(SceneData& data);

  /// Destroy and recreate PBR group frame resources, then rewrite descriptors.
  void rebuild_pbr_descriptors(SceneData& data);

  /// Write IBL descriptors only (set 2).
  void write_ibl_descriptors(SceneData& data);

  vkwave::ExecutionGroup& pbr_group();
  vkwave::ExecutionGroup& composite_group();
  /// The transmission group, or nullptr when the scene has no glass.
  vkwave::ExecutionGroup* transmission_group();
  vkwave::ImGuiOverlay* imgui_overlay() { return imgui.get(); }

private:
  Engine* m_engine;

  // Whether the current graph structure includes the transmission pass (glass
  // present AND single-sample — phase-1 transmission is e1-only).
  bool m_graph_has_transmission{ false };

  /// (Re)create the scene render pass + register pool resources + add groups +
  /// wire the DAG + build + write descriptors, deciding the transmission pass in
  /// from data.has_transmission() and the current MSAA. Shared by the
  /// constructor and rebuild_graph(). Assumes the graph is in its pre-add state.
  void build_scene_graph(SceneData& data);

  /// Add + configure the transmission offscreen group (color=HDR, depth=shared,
  /// material SSBO descriptor count). Returns the group. Pool resources must
  /// already be registered. Shared by build_scene_graph() and rebuild_for_msaa().
  vkwave::ExecutionGroup& add_transmission_group(SceneData& data);

  // Immutable per-material constants (GpuMaterial[]), shared across all frames.
  // Built once per model load; only the descriptor is rewritten on rebuild.
  std::unique_ptr<vkwave::Buffer> material_buffer;

  /// (Re)build the material SSBO from the active materials and write its
  /// descriptor to set 2. Called from write_pbr_descriptors().
  void upload_material_buffer(SceneData& data);
};
