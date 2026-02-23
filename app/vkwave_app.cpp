#include "app.h"
#include "input.h"
#include "scene.h"

#include <imgui.h>

#include <vulkan/vulkan_to_string.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>

// ---------------------------------------------------------------------------
// GLFW callback context — shared user pointer for all callbacks
// ---------------------------------------------------------------------------

struct Callbacks
{
  App* app;
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

  auto maybe_config = load_config_with_cli(argc, argv);
  if (!maybe_config)
    return EXIT_SUCCESS;
  auto config = std::move(*maybe_config);

  if (config.use_x11)
    setenv("VKWAVE_USE_X11", "1", 1);

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  App app(config);
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
  input.bind(scene.camera);

  spdlog::info("Swapchain images: {}", app.swapchain.image_count());
  spdlog::info("Present mode: {}", vk::to_string(app.swapchain.present_mode()));
  spdlog::info("Display refresh rate: {} Hz", app.window.refresh_rate());

  auto fps_time = std::chrono::steady_clock::now();
  uint64_t fps_frames = 0;
  double avg_fps = 0.0;

  while (!app.should_close() && !app.frame_limit_reached())
  {
    app.poll();

    // Update averaged FPS (title bar + ImGui)
    ++fps_frames;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - fps_time).count();
    if (elapsed >= 0.5)
    {
      avg_fps = fps_frames / elapsed;
      app.window.set_title(fmt::format("{} — {:.0f} fps", config.window_title, avg_fps));
      fps_frames = 0;
      fps_time = now;
    }

    if (app.handle_resize())
    {
      scene.resize(app.swapchain);
      continue;
    }

    scene.update(app.graph);

    scene.imgui->new_frame();
    ImGui::Begin("vkwave");
    ImGui::Text("%.0f fps", avg_fps);
    ImGui::Separator();

    // PBR debug modes
    const char* debug_modes[] = {
      "Final", "Normals", "Base Color", "Metallic",
      "Roughness", "AO", "Emissive"
    };
    ImGui::Combo("Debug Mode", &scene.pbr_pass.debug_mode, debug_modes, IM_ARRAYSIZE(debug_modes));

    // Tonemapping
    ImGui::Separator();
    const char* tonemap_modes[] = {
      "None", "Reinhard", "ACES (Fast)", "ACES (Hill)",
      "ACES + Boost", "Khronos PBR Neutral"
    };
    ImGui::Combo("Tonemap", &scene.composite_pass.tonemap_mode, tonemap_modes, IM_ARRAYSIZE(tonemap_modes));
    ImGui::SliderFloat("Exposure", &scene.composite_pass.exposure, 0.1f, 5.0f);

    // IBL environment
    if (!app.config.hdr_paths.empty())
    {
      ImGui::Separator();
      ImGui::Text("Environment");
      auto current_label = (scene.current_hdr_index >= 0
            && scene.current_hdr_index < static_cast<int>(app.config.hdr_paths.size()))
          ? std::filesystem::path(app.config.hdr_paths[scene.current_hdr_index]).stem().string()
          : std::string("neutral");
      if (ImGui::BeginCombo("HDR", current_label.c_str()))
      {
        for (int i = 0; i < static_cast<int>(app.config.hdr_paths.size()); ++i)
        {
          auto label = std::filesystem::path(app.config.hdr_paths[i]).stem().string();
          bool selected = (i == scene.current_hdr_index);
          if (ImGui::Selectable(label.c_str(), selected))
          {
            if (i != scene.current_hdr_index)
            {
              scene.switch_ibl(app.config.hdr_paths[i]);
              scene.current_hdr_index = i;
            }
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    // Light controls
    ImGui::Separator();
    ImGui::Text("Directional Light");
    ImGui::SliderFloat3("Direction", &scene.pbr_pass.light_direction.x, -1.0f, 1.0f);
    ImGui::SliderFloat("Intensity", &scene.pbr_pass.light_intensity, 0.0f, 10.0f);
    ImGui::ColorEdit3("Light Color", &scene.pbr_pass.light_color.x);

    // Material overrides
    ImGui::Separator();
    ImGui::Text("Material Overrides");
    ImGui::SliderFloat("Metallic", &scene.pbr_pass.metallic_factor, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &scene.pbr_pass.roughness_factor, 0.0f, 1.0f);

    ImGui::End();

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
  return EXIT_SUCCESS;
}
