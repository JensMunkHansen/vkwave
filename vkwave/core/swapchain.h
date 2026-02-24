#pragma once

#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>


namespace vkwave
{

class Device;
class Semaphore;

class Swapchain
{
private:
  Device& m_device;

  vk::SurfaceKHR m_surface{ VK_NULL_HANDLE };
  std::optional<vk::SurfaceFormatKHR> m_surface_format{};
  // images
  std::vector<vk::Image> m_imgs;
  // image views
  std::vector<vk::ImageView> m_img_views;

  vk::Extent2D m_extent{};
  vk::SwapchainKHR m_swapchain{ nullptr };
  vk::PresentModeKHR m_present_mode{ vk::PresentModeKHR::eFifo };
  std::vector<vk::PresentModeKHR> m_available_present_modes;

  //  std::unique_ptr<Semaphore> m_img_available;
  [[nodiscard]] std::vector<vk::Image> get_swapchain_images();
  bool m_vsync_enabled{ true };
  std::optional<vk::PresentModeKHR> m_preferred_present_mode{};
  uint32_t m_preferred_image_count{ 0 }; // 0 = driver default

  std::optional<vk::CompositeAlphaFlagBitsKHR> choose_composite_alpha(
    const vk::CompositeAlphaFlagBitsKHR request_composite_alpha,
    const vk::CompositeAlphaFlagsKHR supported_composite_alpha);

  vk::Extent2D choose_image_extent(const vk::Extent2D& requested_extent,
    const vk::Extent2D& min_extent, const vk::Extent2D& max_extent,
    const vk::Extent2D& current_extent);

  // Make this default to mailbox
  vk::PresentModeKHR choose_present_mode(
    const std::vector<vk::PresentModeKHR>& available_present_modes,
    const std::vector<vk::PresentModeKHR>& present_mode_priority_list, const bool vsync_enabled);

  std::optional<vk::SurfaceFormatKHR> choose_surface_format(
    const std::vector<vk::SurfaceFormatKHR>& available_formats,
    const std::vector<vk::SurfaceFormatKHR>& format_prioriy_list = {});

  void setup_swapchain(
    const std::uint32_t width, const std::uint32_t height, const bool vsync_enabled);

public:
  void recreate(std::uint32_t width, std::uint32_t height)
  {
    setup_swapchain(width, height, m_vsync_enabled);
  }

  void set_vsync(bool enabled) { m_vsync_enabled = enabled; }

  void set_preferred_present_mode(vk::PresentModeKHR mode) { m_preferred_present_mode = mode; }
  void set_preferred_image_count(uint32_t count) { m_preferred_image_count = count; }

  Swapchain(Device& device, VkSurfaceKHR surface, std::uint32_t width, std::uint32_t height,
    bool vsync_enabled,
    std::optional<vk::PresentModeKHR> preferred_present_mode = std::nullopt,
    uint32_t preferred_image_count = 0);

  Swapchain(const Swapchain&) = delete;
  Swapchain(Swapchain&&) noexcept;

  ~Swapchain();

  Swapchain& operator=(const Swapchain&) = delete;
  Swapchain& operator=(Swapchain&&) = delete;

  [[nodiscard]] const vk::Extent2D extent() const { return m_extent; }
  [[nodiscard]] std::uint32_t image_count() const
  {
    return static_cast<std::uint32_t>(m_imgs.size());
  }

  [[nodiscard]] vk::Format image_format() const { return m_surface_format.value().format; }

  [[nodiscard]] const std::vector<vk::ImageView>& image_views() const { return m_img_views; }
  [[nodiscard]] const std::vector<vk::Image>& images() const { return m_imgs; }
  [[nodiscard]] const vk::SwapchainKHR* swapchain() const { return &m_swapchain; }
  [[nodiscard]] vk::PresentModeKHR present_mode() const { return m_present_mode; }
  [[nodiscard]] const std::vector<vk::PresentModeKHR>& available_present_modes() const
  {
    return m_available_present_modes;
  }
};
};
