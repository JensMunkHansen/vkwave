#include "engine.h"
#include "input.h"
#include "scene.h"

#include <vkwave/pipeline/shader_compiler.h>

#include <imgui.h>

#include <vulkan/vulkan_to_string.hpp>

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>

// ---------------------------------------------------------------------------
// GLFW callback context — shared user pointer for all callbacks
// ---------------------------------------------------------------------------

struct Callbacks
{
  Engine* app;
  Input* input;
};

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
  static constexpr bool kDebug =
#ifdef VKWAVE_DEBUG
    true;
#else
    false;
#endif

  spdlog::set_level(kDebug ? spdlog::level::debug : spdlog::level::info);
  spdlog::info("vkwave -- async GPU rendering engine");

  try {

  auto maybe_config = load_config_with_cli(argc, argv);
  if (!maybe_config)
    return EXIT_SUCCESS;
  auto config = std::move(*maybe_config);

#ifndef _WIN32
  if (config.use_x11)
    setenv("VKWAVE_USE_X11", "1", 1);
#endif

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // Create and configure shader compiler before anything builds pipelines
  auto compiler = vkwave::ShaderCompiler::create();
  compiler->set_debug_info(kDebug || config.shader_debug);
  compiler->set_optimization(!kDebug && config.shader_optimize);

  Engine app(config);
  app.set_shader_compiler(compiler);
  Input input;

  g_window = app.window.get();

  // Register GLFW callbacks BEFORE Scene construction so ImGui
  // (init with install_callbacks=true) chains to them automatically.
  Callbacks ctx{&app, &input};
  app.window.set_user_ptr(&ctx);

  app.window.set_resize_callback([](GLFWwindow* w, int /*width*/, int /*height*/) {
    int fb_w, fb_h;
    glfwGetFramebufferSize(w, &fb_w, &fb_h);
    auto* c = static_cast<Callbacks*>(glfwGetWindowUserPointer(w));
    c->app->window.set_resize_pending(
      static_cast<uint32_t>(fb_w), static_cast<uint32_t>(fb_h));
  });

  glfwSetCursorPosCallback(app.window.get(), [](GLFWwindow* w, double xpos, double ypos) {
    auto* c = static_cast<Callbacks*>(glfwGetWindowUserPointer(w));
    c->input->on_cursor_pos(w, xpos, ypos);
  });

  glfwSetScrollCallback(app.window.get(), [](GLFWwindow* w, double /*xoffset*/, double yoffset) {
    auto* c = static_cast<Callbacks*>(glfwGetWindowUserPointer(w));
    c->input->on_scroll(yoffset);
  });

  Scene scene(app);

  // Populate scene data -- explicit, not hidden in a constructor
  scene.data.create_fallback_textures(app.device);
  scene.data.load_model(app.device, app.config.model_path);
  scene.data.load_ibl(app.device, app.config.hdr_path);

  // Track which config entries are active (for UI combo boxes)
  for (int i = 0; i < static_cast<int>(app.config.model_paths.size()); ++i)
  {
    if (app.config.model_paths[i] == app.config.model_path)
    { scene.data.current_model_index = i; break; }
  }
  scene.data.current_hdr_index = -1;
  for (int i = 0; i < static_cast<int>(app.config.hdr_paths.size()); ++i)
  {
    if (app.config.hdr_paths[i] == app.config.hdr_path)
    { scene.data.current_hdr_index = i; break; }
  }
  if (scene.data.current_hdr_index < 0 && !app.config.hdr_paths.empty())
    scene.data.current_hdr_index = 0;

  scene.data.camera.set_position(0.0f, 1.5f, 3.0f);
  scene.data.camera.set_focal_point(0.0f, 0.0f, 0.0f);
  scene.data.camera.set_aspect_ratio(
    static_cast<float>(app.swapchain.extent().width) /
    static_cast<float>(app.swapchain.extent().height));

  // Build rendering pipeline from populated data
  scene.build_pipeline();
  input.bind(scene.data.camera);

  spdlog::info("Swapchain images: {}", app.swapchain.image_count());
  spdlog::info("Present mode: {}", vk::to_string(app.swapchain.present_mode()));
  spdlog::info("Display refresh rate: {} Hz", app.window.refresh_rate());

  while (!app.should_close() && !app.frame_limit_reached())
  {
    app.poll();

    if (app.handle_resize())
    {
      scene.resize(app.swapchain);
      continue;
    }

    double avg_fps = app.update_fps();
    scene.update(app.graph);
    scene.draw_ui(app, avg_fps);

    if (!app.render_frame())
    {
      ImGui::EndFrame();
      app.window.set_resize_pending(app.window.width(), app.window.height());
      continue;
    }

    // When present was skipped (rate-gated), Render() wasn't called —
    // EndFrame() closes the ImGui frame. Safe to call after Render() too (no-op).
    ImGui::EndFrame();
  }

  // Drain GPU before scene destroys its mesh buffers
  app.graph.drain();

  spdlog::info("Exiting after {} frames", app.graph.cpu_frame());

  } catch (const vk::SystemError& e) {
    spdlog::critical("Vulkan error: {}", e.what());
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    spdlog::critical("Fatal error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
