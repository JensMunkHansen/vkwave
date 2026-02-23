#pragma once

#include <vkwave/core/window.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct AppConfig
{
  // [vulkan]
  std::string preferred_gpu;
  std::string present_mode{ "mailbox" }; // "immediate", "mailbox", "fifo", "fifo_relaxed"
  uint32_t swapchain_images{ 0 };        // 0 = driver default

  // [window]
  std::string window_title{ "vkwave" };
  uint32_t window_width{ 800 };
  uint32_t window_height{ 600 };
  std::string window_mode{ "windowed" };

  // [platform]
  bool use_x11{ false };

  // [debug]
  uint64_t max_frames{ 0 };  // 0 = unlimited, >0 = exit after N frames
};

AppConfig load_config(const std::string& path);

/// Load config from TOML, then apply CLI overrides.
/// Returns nullopt if the program should exit (help/completion was printed).
std::optional<AppConfig> load_config_with_cli(int argc, char** argv);

vkwave::Window::Mode parse_window_mode(const std::string& mode);
std::optional<vk::PresentModeKHR> parse_present_mode(const std::string& mode);
