#include "app_config.h"

#include <vkwave/config.h>

#include <vkwave/core/device.h>
#include <vkwave/core/instance.h>
#include <vkwave/core/push_constants.h>
#include <vkwave/core/swapchain.h>
#include <vkwave/core/window.h>
#include <vkwave/core/windowsurface.h>
#include <vkwave/pipeline/pipeline.h>
#include <vkwave/pipeline/render_graph.h>
#include <vkwave/pipeline/triangle_pass.h>

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

  // Pipeline (shared immutable — safe for overlapping frames)
  vk::PipelineLayout pipeline_layout{ VK_NULL_HANDLE };
  vk::RenderPass renderpass{ VK_NULL_HANDLE };
  vk::Pipeline pipeline{ VK_NULL_HANDLE };

  // Render graph (replaces flat per-frame resources + fences)
  vkwave::RenderGraph graph;

  // Pass (trivially destructible — holds only raw handles)
  vkwave::TrianglePass triangle_pass{};

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

    create_pipeline();

    triangle_pass.pipeline = pipeline;
    triangle_pass.layout = pipeline_layout;
    triangle_pass.renderpass = renderpass;
    triangle_pass.extent = swapchain.extent();

    // Set up render graph with one execution group
    auto& group = graph.add_group("triangle", pipeline, pipeline_layout, renderpass);
    group.set_record_fn([this](vk::CommandBuffer cmd, vk::Framebuffer fb, vk::Extent2D) {
      const float t = static_cast<float>(graph.cpu_frame()) * 0.02f;
      vkwave::TrianglePushConstants pc{};
      pc.color[0] = 0.5f + 0.5f * std::sin(t);
      pc.color[1] = 0.5f + 0.5f * std::sin(t + 2.094f);
      pc.color[2] = 0.5f + 0.5f * std::sin(t + 4.189f);
      pc.color[3] = 1.0f;
      pc.time = t;
      pc.debugMode = 0;

      vk::ClearValue clear_color{
        std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }
      };

      vk::RenderPassBeginInfo rp_info{};
      rp_info.renderPass = triangle_pass.renderpass;
      rp_info.framebuffer = fb;
      rp_info.renderArea.extent = triangle_pass.extent;
      rp_info.clearValueCount = 1;
      rp_info.pClearValues = &clear_color;

      cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);
      triangle_pass.record(cmd, pc);
      cmd.endRenderPass();
    });

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
    destroy_pipeline();
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

  void create_pipeline()
  {
    vkwave::GraphicsPipelineInBundle spec{};
    spec.device = device.device();
    spec.vertexFilepath = SHADER_DIR "fullscreen.vert.spv";
    spec.fragmentFilepath = SHADER_DIR "fullscreen.frag.spv";
    spec.swapchainExtent = swapchain.extent();
    spec.swapchainImageFormat = swapchain.image_format();
    spec.backfaceCulling = false;
    spec.pushConstantRanges = {
      { vk::ShaderStageFlagBits::eFragment, 0,
        sizeof(vkwave::TrianglePushConstants) }
    };

    auto bundle = vkwave::create_graphics_pipeline(spec, kDebug);
    pipeline_layout = bundle.layout;
    renderpass = bundle.renderpass;
    pipeline = bundle.pipeline;
  }

  void destroy_pipeline()
  {
    auto d = device.device();
    d.destroyPipeline(pipeline);
    d.destroyPipelineLayout(pipeline_layout);
    d.destroyRenderPass(renderpass);
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
  app.graph.resize(app.swapchain);
  app.triangle_pass.extent = app.swapchain.extent();

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

int main(int /*argc*/, char** /*argv*/)
{
  spdlog::set_level(kDebug ? spdlog::level::debug : spdlog::level::info);
  spdlog::info("vkwave -- async GPU rendering engine");

  auto config = load_config("vkwave.toml");

  if (config.use_x11)
    setenv("VKWAVE_USE_X11", "1", 1);

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  AppState app(config);
  g_window = app.window.get();

  spdlog::info("Swapchain images: {}", app.swapchain.image_count());

  auto fps_time = std::chrono::steady_clock::now();
  uint64_t fps_frames = 0;

  while (!app.window.should_close())
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

    // 1. Acquire swapchain image
    vkwave::RenderGraph::FrameInfo frame_info{};
    try
    {
      frame_info = app.graph.begin_frame(app.swapchain);
    }
    catch (vk::OutOfDateKHRError&)
    {
      app.window.set_resize_pending(app.window.width(), app.window.height());
      continue;
    }

    // 2. Wait, record, submit via the execution group
    auto& group = app.graph.group(0);
    group.begin_frame(frame_info.image_index);
    group.submit(frame_info.image_index,
      app.graph.acquire_semaphore(frame_info.sem_index),
      app.device.graphics_queue());

    // 3. Present
    try
    {
      app.graph.end_frame(app.swapchain, frame_info.image_index,
        app.device.graphics_queue(), app.device.present_queue());
    }
    catch (vk::OutOfDateKHRError&)
    {
      app.window.set_resize_pending(app.window.width(), app.window.height());
    }
  }

  spdlog::info("Exiting after {} frames", app.graph.cpu_frame());
  return EXIT_SUCCESS;
}
