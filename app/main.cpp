#include "app_config.h"

#include <vkwave/config.h>

#include <vkwave/core/camera.h>
#include <vkwave/core/device.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/mesh.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/core/window.h>
#include <vkwave/core/windowsurface.h>
#include <vkwave/pipeline/cube_pass.h>
#include <vkwave/pipeline/render_graph.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <spdlog/fmt/fmt.h>
#include <span>

// Dynamic dispatch storage (required by VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static constexpr bool kDebug =
#ifdef VKWAVE_DEBUG
  true;
#else
  false;
#endif

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

struct AppState;
static void handle_resize(AppState& app);

// ---------------------------------------------------------------------------
// Application state — owns all RAII resources
// ---------------------------------------------------------------------------

struct AppState
{
  // Core objects
  vkwave::Window window;
  vkwave::Instance instance;
  vkwave::WindowSurface surface;
  vkwave::Device device;
  vkwave::Swapchain swapchain;

  // Render graph (owns execution groups, which own pipelines + depth + frame resources)
  vkwave::RenderGraph graph;

  // Mesh (RAII)
  std::unique_ptr<vkwave::Mesh> cube_mesh;

  // Camera
  vkwave::Camera camera;

  // Pass (trivially destructible — holds only raw pointers)
  vkwave::CubePass cube_pass{};

  // Config
  AppConfig config;

  explicit AppState(const AppConfig& cfg)
    : window(cfg.window_title, cfg.window_width, cfg.window_height,
        true, true, parse_window_mode(cfg.window_mode))
    , instance(cfg.window_title.c_str(), cfg.window_title.c_str(),
        VK_MAKE_API_VERSION(0, 0, 1, 0),
        VK_MAKE_API_VERSION(0, 0, 1, 0),
        kDebug, false)
    , surface(instance.instance(), window.get())
    , device(create_device(cfg.preferred_gpu))
    , swapchain(device, surface.get(), window.width(), window.height(), false)
    , graph(device)
    , config(cfg)
  {
    // Apply preferred present mode and image count (recreates swapchain if needed)
    auto pmode = parse_present_mode(cfg.present_mode);
    bool needs_recreate = false;
    if (pmode.has_value())
    {
      swapchain.set_preferred_present_mode(*pmode);
      needs_recreate = true;
    }
    if (cfg.swapchain_images > 0)
    {
      swapchain.set_preferred_image_count(cfg.swapchain_images);
      needs_recreate = true;
    }
    if (needs_recreate)
      swapchain.recreate(window.width(), window.height());

    cube_mesh = vkwave::Mesh::create_cube(device);

    // Set up camera
    camera.set_position(0.0f, 1.5f, 3.0f);
    camera.set_focal_point(0.0f, 0.0f, 0.0f);
    camera.set_aspect_ratio(
      static_cast<float>(swapchain.extent().width) / static_cast<float>(swapchain.extent().height));

    // Register pass — creates group, wires pass, sets record callback
    graph.add_pass("cube", cube_pass, swapchain.image_format(), kDebug);
    cube_pass.mesh = cube_mesh.get();

    graph.build(swapchain);

    // Resize callback
    window.set_user_ptr(this);
    window.set_resize_callback([](GLFWwindow* w, int /*width*/, int /*height*/) {
      int fb_w, fb_h;
      glfwGetFramebufferSize(w, &fb_w, &fb_h);
      auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(w));
      app->window.set_resize_pending(
        static_cast<uint32_t>(fb_w), static_cast<uint32_t>(fb_h));
    });
  }

  ~AppState()
  {
    graph.drain();
  }

  // Non-copyable, non-movable
  AppState(const AppState&) = delete;
  AppState& operator=(const AppState&) = delete;

private:
  vkwave::Device create_device(const std::string& preferred_gpu)
  {
    static const char* extensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    std::span<const char*> ext_span(extensions);

    vk::PhysicalDeviceFeatures required_features{};

    auto physical_device = vkwave::Device::pick_best_physical_device(
      instance, surface.get(), required_features, ext_span, preferred_gpu);

    return vkwave::Device(
      instance, surface.get(), false, physical_device, ext_span,
      required_features, {}, false);
  }
};

// ---------------------------------------------------------------------------
// Resize handler
// ---------------------------------------------------------------------------

static void handle_resize(AppState& app)
{
  uint32_t w, h;
  app.window.get_pending_resize(w, h);

  if (w == 0 || h == 0)
  {
    app.window.wait_for_focus();
    app.window.get_framebuffer_size(w, h);
    if (w == 0 || h == 0)
      return;
  }

  app.graph.drain();
  app.swapchain.recreate(w, h);

  // Group internally recreates depth buffer + framebuffers on resize.
  // Pass queries group->extent(), so no manual update needed.
  app.graph.resize(app.swapchain);

  spdlog::info("Resized to {}x{}", w, h);
}

// ---------------------------------------------------------------------------
// Signal handling — graceful shutdown on SIGINT / SIGTERM
// ---------------------------------------------------------------------------

static GLFWwindow* g_window = nullptr;

static void signal_handler(int /*sig*/)
{
  if (g_window)
    glfwSetWindowShouldClose(g_window, GLFW_TRUE);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
  spdlog::set_level(kDebug ? spdlog::level::debug : spdlog::level::info);
  spdlog::info("vkwave -- async GPU rendering engine");

  auto config = load_config("vkwave.toml");

  // Command line: --max-frames N overrides config
  for (int i = 1; i < argc - 1; ++i)
  {
    if (std::string_view(argv[i]) == "--max-frames")
      config.max_frames = std::strtoull(argv[++i], nullptr, 10);
  }

  if (config.use_x11)
    setenv("VKWAVE_USE_X11", "1", 1);

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  AppState app(config);
  g_window = app.window.get();

  spdlog::info("Swapchain images: {}", app.swapchain.image_count());

  auto fps_time = std::chrono::steady_clock::now();
  uint64_t fps_frames = 0;

  while (!app.window.should_close() &&
         (config.max_frames == 0 || app.graph.cpu_frame() < config.max_frames))
  {
    vkwave::Window::poll();

    // Update title bar FPS
    ++fps_frames;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - fps_time).count();
    if (elapsed >= 0.5)
    {
      double fps = fps_frames / elapsed;
      app.window.set_title(fmt::format("{} — {:.0f} fps", config.window_title, fps));
      fps_frames = 0;
      fps_time = now;
    }

    // Handle resize
    if (app.window.has_pending_resize())
    {
      handle_resize(app);
      continue;
    }

    // Update per-frame pass state
    app.camera.azimuth(0.5f);
    app.camera.set_aspect_ratio(
      static_cast<float>(app.graph.group(0).extent().width) /
      static_cast<float>(app.graph.group(0).extent().height));
    app.cube_pass.view_projection = app.camera.view_projection_matrix();
    app.cube_pass.time = static_cast<float>(app.graph.cpu_frame()) * 0.02f;

    // Acquire, record, submit, present
    if (!app.graph.render_frame(app.swapchain))
    {
      app.window.set_resize_pending(app.window.width(), app.window.height());
      continue;
    }
  }

  spdlog::info("Exiting after {} frames", app.graph.cpu_frame());
  return EXIT_SUCCESS;
}
