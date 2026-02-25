#include "engine.h"

#include <vkwave/config.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cassert>
#include <span>

static constexpr bool kDebug =
#ifdef VKWAVE_DEBUG
  true;
#else
  false;
#endif

Engine::Engine(const AppConfig& cfg)
  : window(cfg.window_title, cfg.window_width, cfg.window_height,
      true, true, parse_window_mode(cfg.window_mode))
  , instance(cfg.window_title.c_str(), cfg.window_title.c_str(),
      VK_MAKE_API_VERSION(0, 0, 1, 0),
      VK_MAKE_API_VERSION(0, 0, 1, 0),
      kDebug, false)
  , surface(instance.instance(), window.get())
  , device(create_device(cfg.preferred_gpu))
  , swapchain(device, surface.get(), window.width(), window.height(), false,
      parse_present_mode(cfg.present_mode), cfg.swapchain_images)
  , graph(device)
  , config(cfg)
{
}

Engine::~Engine()
{
  graph.drain();
}

bool Engine::render_frame()
{
  return graph.render_frame(swapchain);
}

double Engine::update_fps()
{
  ++m_fps_frames;
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration<double>(now - m_fps_time).count();
  if (elapsed >= 0.5)
  {
    m_avg_fps = m_fps_frames / elapsed;
    window.set_title(fmt::format("{} — {:.0f} fps", config.window_title, m_avg_fps));
    m_fps_frames = 0;
    m_fps_time = now;
  }
  return m_avg_fps;
}

bool Engine::handle_resize()
{
  if (!window.has_pending_resize())
    return false;

  uint32_t w, h;
  window.get_pending_resize(w, h);

  if (w == 0 || h == 0)
  {
    window.wait_for_focus();
    window.get_framebuffer_size(w, h);
    if (w == 0 || h == 0)
      return true;  // Still minimized — skip frame
  }

  graph.drain();
  swapchain.recreate(w, h);
  graph.resize(swapchain);

  spdlog::info("Resized to {}x{}", w, h);
  return true;
}

void Engine::set_shader_compiler(std::shared_ptr<vkwave::ShaderCompiler> compiler)
{
  m_shader_compiler = std::move(compiler);
}

vkwave::ShaderCompiler& Engine::shader_compiler()
{
  assert(m_shader_compiler && "ShaderCompiler not set — call set_shader_compiler() first");
  return *m_shader_compiler;
}

vkwave::Device Engine::create_device(const std::string& preferred_gpu)
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
