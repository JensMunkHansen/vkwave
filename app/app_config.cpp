#include "app_config.h"
#include "cli.h"

#include <toml.hpp>

#include <spdlog/spdlog.h>

#include <filesystem>

AppConfig load_config(const std::string& path)
{
  AppConfig cfg;

  if (!std::filesystem::exists(path))
  {
    spdlog::warn("Config file '{}' not found, using defaults", path);
    return cfg;
  }

  try
  {
    auto data = toml::parse(path);

    // [vulkan]
    if (data.contains("vulkan"))
    {
      auto& vulkan = toml::find(data, "vulkan");
      cfg.preferred_gpu = toml::find_or(vulkan, "preferred_gpu", std::string{});
      cfg.present_mode = toml::find_or(vulkan, "present_mode", std::string{ "mailbox" });
      cfg.swapchain_images = toml::find_or<uint32_t>(vulkan, "swapchain_images", 0);
    }

    // [window]
    if (data.contains("window"))
    {
      auto& window = toml::find(data, "window");
      cfg.window_title = toml::find_or(window, "title", std::string{ "vkwave" });
      cfg.window_width = toml::find_or<uint32_t>(window, "width", 800);
      cfg.window_height = toml::find_or<uint32_t>(window, "height", 600);
      cfg.window_mode = toml::find_or(window, "mode", std::string{ "windowed" });
    }

    // [platform]
    if (data.contains("platform"))
    {
      auto& platform = toml::find(data, "platform");
      cfg.use_x11 = toml::find_or(platform, "use_x11", false);
    }

    // [scene]
    if (data.contains("scene"))
    {
      auto& scene = toml::find(data, "scene");
      cfg.model_path = toml::find_or(scene, "model_path", std::string{});
      if (scene.contains("model_paths"))
      {
        auto arr = toml::find<std::vector<std::string>>(scene, "model_paths");
        cfg.model_paths = std::move(arr);
      }
      cfg.hdr_path = toml::find_or(scene, "hdr_path", std::string{});
      if (scene.contains("hdr_paths"))
      {
        auto arr = toml::find<std::vector<std::string>>(scene, "hdr_paths");
        cfg.hdr_paths = std::move(arr);
      }
    }

    // [debug]
    if (data.contains("debug"))
    {
      auto& debug = toml::find(data, "debug");
      cfg.max_frames = toml::find_or<uint64_t>(debug, "max_frames", 0);
    }

    spdlog::info("Loaded config from '{}'", path);
  }
  catch (const std::exception& e)
  {
    spdlog::error("Failed to parse config '{}': {}", path, e.what());
    spdlog::warn("Using default configuration");
    return AppConfig{};
  }

  return cfg;
}

std::optional<AppConfig> load_config_with_cli(int argc, char** argv)
{
  // First pass: extract --config path (and check for --help / --complete)
  AppConfig config;
  std::string config_path = "vkwave.toml";
  if (!parse_cli(argc, argv, config, config_path))
    return std::nullopt;

  // Load TOML config
  config = load_config(config_path);

  // Second pass: CLI flags override config file values
  parse_cli(argc, argv, config, config_path);

  return config;
}

vkwave::Window::Mode parse_window_mode(const std::string& mode)
{
  if (mode == "fullscreen") return vkwave::Window::Mode::FULLSCREEN;
  if (mode == "windowed_fullscreen") return vkwave::Window::Mode::WINDOWED_FULLSCREEN;
  return vkwave::Window::Mode::WINDOWED;
}

std::optional<vk::PresentModeKHR> parse_present_mode(const std::string& mode)
{
  if (mode == "immediate") return vk::PresentModeKHR::eImmediate;
  if (mode == "mailbox") return vk::PresentModeKHR::eMailbox;
  if (mode == "fifo") return vk::PresentModeKHR::eFifo;
  if (mode == "fifo_relaxed") return vk::PresentModeKHR::eFifoRelaxed;
  return std::nullopt;
}
