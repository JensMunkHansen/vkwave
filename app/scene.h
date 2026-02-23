#pragma once

#include <vkwave/core/camera.h>
#include <vkwave/core/mesh.h>
#include <vkwave/pipeline/composite_pass.h>
#include <vkwave/pipeline/cube_pass.h>
#include <vkwave/pipeline/imgui_overlay.h>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

struct App;
namespace vkwave { class RenderGraph; class Swapchain; }

/// Offscreen HDR image (RAII).
struct OffscreenImage
{
  vk::Image image{ VK_NULL_HANDLE };
  vk::DeviceMemory memory{ VK_NULL_HANDLE };
  vk::ImageView view{ VK_NULL_HANDLE };
};

/// Scene objects: camera, meshes, passes, offscreen resources.
struct Scene
{
  vkwave::Camera camera;
  std::unique_ptr<vkwave::Mesh> cube_mesh;
  vkwave::CubePass cube_pass{};
  vkwave::CompositePass composite_pass{};
  std::unique_ptr<vkwave::ImGuiOverlay> imgui;

  // Offscreen HDR resources (owned by Scene, not by passes)
  static constexpr vk::Format kHdrFormat = vk::Format::eR16G16B16A16Sfloat;
  std::vector<OffscreenImage> hdr_images;
  vk::Sampler hdr_sampler{ VK_NULL_HANDLE };
  vk::RenderPass scene_renderpass{ VK_NULL_HANDLE };
  vk::RenderPass composite_renderpass{ VK_NULL_HANDLE };

  explicit Scene(App& app);
  ~Scene();

  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  /// Recreate size-dependent resources after swapchain resize.
  void resize(const vkwave::Swapchain& swapchain);

  /// Update per-frame pass state (aspect ratio, model matrix, time).
  void update(vkwave::RenderGraph& graph);

private:
  App* m_app;

  void create_hdr_images(vk::Extent2D extent, uint32_t count);
  void destroy_hdr_images();
  void create_sampler();
};
