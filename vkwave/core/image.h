#pragma once

#include <vulkan/vulkan.hpp>

#include <string>

namespace vkwave
{

class Device;

/// RAII wrapper for a device-local color image with view.
///
/// Used for offscreen render targets (HDR images, intermediate buffers).
/// Creates a VkImage + VkDeviceMemory + VkImageView and destroys them
/// in the destructor.
class Image
{
public:
  /// Create a device-local image with a color image view.
  /// @param device  Vulkan device wrapper.
  /// @param format  Image format (e.g. R16G16B16A16Sfloat for HDR).
  /// @param extent  Image dimensions.
  /// @param usage   Usage flags (e.g. eColorAttachment | eSampled).
  /// @param name    Debug name.
  /// @param samples MSAA sample count (default e1). When multisample,
  ///                eTransientAttachment is added automatically.
  Image(const Device& device, vk::Format format, vk::Extent2D extent,
    vk::ImageUsageFlags usage, const std::string& name,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);

  ~Image();

  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  Image(Image&& other) noexcept;
  Image& operator=(Image&& other) noexcept;

  [[nodiscard]] vk::Image image() const { return m_image; }
  [[nodiscard]] vk::ImageView image_view() const { return m_view; }
  [[nodiscard]] vk::Format format() const { return m_format; }
  [[nodiscard]] vk::Extent2D extent() const { return m_extent; }

private:
  void destroy();

  vk::Device m_device;
  vk::Image m_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_memory{ VK_NULL_HANDLE };
  vk::ImageView m_view{ VK_NULL_HANDLE };
  vk::Format m_format{};
  vk::Extent2D m_extent{};
};

} // namespace vkwave
