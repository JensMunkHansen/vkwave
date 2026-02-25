#pragma once

#include "scene_data.h"
#include "scene_pipeline.h"

#include <vkwave/core/buffer.h>
#include <vkwave/core/fence.h>
#include <vkwave/pipeline/composite_pass.h>
#include <vkwave/pipeline/pbr_pass.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct Engine;
namespace vkwave { class RenderGraph; class Swapchain; }

/// Scene: thin composition of SceneData (assets) + ScenePipeline (infrastructure)
/// plus trivially-destructible pass structs that bridge them.
struct Scene
{
  SceneData data;
  std::unique_ptr<ScenePipeline> pipeline;

  // Pass state (trivially destructible -- raw handles + POD)
  vkwave::PBRContext pbr_ctx{};
  vkwave::PBRPass pbr_pass{};
  vkwave::BlendPass blend_pass{};
  vkwave::CompositePass composite_pass{};

  // Screenshot: captures from offscreen HDR image, fence-based polling,
  // single grow-only HOST_VISIBLE readback buffer, worker thread for PNG.
  bool screenshot_requested{ false };
  bool screenshot_in_flight{ false };       // GPU copy submitted, fence not yet signaled
  bool screenshot_compressing{ false };     // worker thread compressing PNG
  // Serializes only the screenshot copy, not frames — GPU keeps pipelining.
  std::unique_ptr<vkwave::Fence> screenshot_fence;
  std::unique_ptr<vkwave::Buffer> screenshot_readback;  // HOST_VISIBLE, grow-only
  vk::Extent2D screenshot_extent{};         // extent at capture time
  vk::Format screenshot_format{};           // HDR format at capture time
  std::thread screenshot_thread;
  std::vector<uint8_t> screenshot_png;
  std::string screenshot_filename;

  explicit Scene(Engine& engine);
  ~Scene();

  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  /// Build the rendering pipeline from the current state of data.
  /// Call after populating data (load_model, load_ibl, fallback textures, camera).
  void build_pipeline();

  /// Recreate size-dependent resources after swapchain resize.
  void resize(const vkwave::Swapchain& swapchain);

  /// Update per-frame pass state (aspect ratio, model matrix, time).
  void update(vkwave::RenderGraph& graph);

  /// Draw the ImGui control panel. Called between imgui->new_frame() and render.
  void draw_ui(Engine& engine, double avg_fps);

  /// Switch to a different HDR environment at runtime.
  void switch_ibl(const std::string& hdr_path);

  /// Switch to a different glTF model at runtime.
  void switch_model(const std::string& model_path);

  /// Rebuild render passes and pipelines when MSAA changes.
  void rebuild_pipeline(vk::SampleCountFlagBits new_samples);

  /// Ensure HOST_VISIBLE readback buffer is large enough. Grow-only, never freed.
  void ensure_screenshot_readback(vk::DeviceSize needed);

private:
  Engine* m_engine;

  /// Wire PBR context pointers from data into pass structs.
  void wire_pbr_context();

  /// Set record/post-record lambdas on execution groups.
  void wire_record_callbacks();
};
