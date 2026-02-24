#pragma once

#include <vkwave/core/camera.h>
#include <vkwave/core/image.h>
#include <vkwave/core/mesh.h>
#include <vkwave/core/texture.h>
#include <vkwave/loaders/gltf_loader.h>
#include <vkwave/loaders/ibl.h>
#include <vkwave/pipeline/composite_pass.h>
#include <vkwave/pipeline/pbr_pass.h>
#include <vkwave/pipeline/imgui_overlay.h>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>
#include <vector>

struct Engine;
namespace vkwave { class RenderGraph; class Swapchain; }

/// Scene objects: camera, meshes, passes, offscreen resources.
struct Scene
{
  vkwave::Camera camera;

  // PBR model + environment
  vkwave::GltfScene gltf_scene;
  vkwave::GltfModel gltf_model;             // legacy single-material fallback
  std::unique_ptr<vkwave::Mesh> cube_mesh;  // fallback when no model_path
  std::unique_ptr<vkwave::IBL> ibl;

  // Fallback 1x1 textures for missing material slots
  std::unique_ptr<vkwave::Texture> fallback_white;
  std::unique_ptr<vkwave::Texture> fallback_normal;
  std::unique_ptr<vkwave::Texture> fallback_mr;
  std::unique_ptr<vkwave::Texture> fallback_black;

  // Shared PBR state (owned here, read by passes)
  vkwave::PBRContext pbr_ctx{};

  // Passes
  vkwave::PBRPass pbr_pass{};
  vkwave::BlendPass blend_pass{};
  vkwave::CompositePass composite_pass{};
  std::unique_ptr<vkwave::ImGuiOverlay> imgui;

  // Offscreen HDR resources (owned by Scene, not by passes)
  static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;
  std::vector<vkwave::Image> hdr_images;
  vk::Sampler hdr_sampler{ VK_NULL_HANDLE };
  vk::RenderPass scene_renderpass{ VK_NULL_HANDLE };
  vk::RenderPass composite_renderpass{ VK_NULL_HANDLE };

  explicit Scene(Engine& engine);
  ~Scene();

  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  /// Recreate size-dependent resources after swapchain resize.
  void resize(const vkwave::Swapchain& swapchain);

  /// Update per-frame pass state (aspect ratio, model matrix, time).
  void update(vkwave::RenderGraph& graph);

  /// Draw the ImGui control panel. Called between imgui->new_frame() and render.
  void draw_ui(Engine& engine, double avg_fps);

  /// Switch to a different HDR environment. Drains GPU, creates new IBL,
  /// rewrites descriptors. Empty path = default neutral IBL.
  void switch_ibl(const std::string& hdr_path);

  /// Index into config.model_paths for the currently loaded model (-1 = none).
  int current_model_index{ -1 };

  /// Switch to a different glTF model at runtime. Drains GPU, reloads
  /// geometry and materials, rewrites descriptors. Empty path = default cube.
  void switch_model(const std::string& model_path);

  /// Index into config.hdr_paths for the currently loaded IBL (-1 = neutral).
  int current_hdr_index{ 0 };

  /// Current MSAA sample count for the PBR pass (e1 = off).
  vk::SampleCountFlagBits msaa_samples{ vk::SampleCountFlagBits::e1 };

  /// Rebuild render passes and pipelines when MSAA changes.
  void rebuild_pipeline(vk::SampleCountFlagBits new_samples);

private:
  Engine* m_engine;

  void create_hdr_images(const vkwave::Device& device, vk::Extent2D extent, uint32_t count);
  void create_sampler();
  void create_fallback_textures();
  void write_pbr_descriptors(vkwave::ExecutionGroup& group);
};
