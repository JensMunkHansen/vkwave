#pragma once

#include "app_config.h"

#include <vkwave/core/device.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/core/window.h>
#include <vkwave/core/windowsurface.h>
#include <vkwave/pipeline/render_graph.h>
#include <vkwave/pipeline/shader_compiler.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

/// Vulkan infrastructure: window, instance, device, swapchain, render graph.
struct Engine
{
  vkwave::Window window;
  vkwave::Instance instance;
  std::optional<vkwave::WindowSurface> surface;
  std::optional<vkwave::Device> device;
  std::optional<vkwave::Swapchain> swapchain;
  std::optional<vkwave::RenderGraph> graph;
  AppConfig config;

  explicit Engine(const AppConfig& cfg);
  ~Engine();

  /// Run graph.render_frame(). Returns false on swapchain out-of-date.
  bool render_frame();

  /// Check and handle pending resize. Returns true if resize occurred.
  bool handle_resize();

  /// Tick FPS counter, update window title every 0.5s. Returns current average FPS.
  double update_fps();

  void poll() { vkwave::Window::poll(); }
  [[nodiscard]] bool should_close() const { return window.should_close(); }
  [[nodiscard]] bool frame_limit_reached() const
  {
    return config.max_frames > 0 && graph->cpu_frame() >= config.max_frames;
  }

  void set_shader_compiler(std::shared_ptr<vkwave::ShaderCompiler> compiler);
  vkwave::ShaderCompiler& shader_compiler();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

private:
  vkwave::Device create_device(const std::string& preferred_gpu);
  std::shared_ptr<vkwave::ShaderCompiler> m_shader_compiler;

  std::chrono::steady_clock::time_point m_fps_time{ std::chrono::steady_clock::now() };
  uint64_t m_fps_frames{ 0 };
  double m_avg_fps{ 0.0 };
};
