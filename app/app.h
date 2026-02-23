#pragma once

#include "app_config.h"

#include <vkwave/core/device.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/core/window.h>
#include <vkwave/core/windowsurface.h>
#include <vkwave/pipeline/render_graph.h>

/// Vulkan infrastructure: window, instance, device, swapchain, render graph.
struct App
{
  vkwave::Window window;
  vkwave::Instance instance;
  vkwave::WindowSurface surface;
  vkwave::Device device;
  vkwave::Swapchain swapchain;
  vkwave::RenderGraph graph;
  AppConfig config;

  explicit App(const AppConfig& cfg);
  ~App();

  /// Run graph.render_frame(). Returns false on swapchain out-of-date.
  bool render_frame();

  /// Check and handle pending resize. Returns true if resize occurred.
  bool handle_resize();

  void poll() { vkwave::Window::poll(); }
  [[nodiscard]] bool should_close() const { return window.should_close(); }
  [[nodiscard]] bool frame_limit_reached() const
  {
    return config.max_frames > 0 && graph.cpu_frame() >= config.max_frames;
  }

  App(const App&) = delete;
  App& operator=(const App&) = delete;

private:
  vkwave::Device create_device(const std::string& preferred_gpu);
};
