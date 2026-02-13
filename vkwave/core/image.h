#pragma once

#include <vulkan/vulkan.hpp>

#include <string>

namespace vkwave
{
class Device;
class Image
{
  const Device& m_device;
  vk::Image m_image{ VK_NULL_HANDLE };
  vk::Format m_format{ VK_FORMAT_UNDEFINED };
  vk::ImageView m_image_view{ VK_NULL_HANDLE };
  std::string m_name;

public:
  /// @brief Default constructor.
  /// @param device The const reference to a device RAII wrapper instance.
  /// @param format The color format.
  /// @param image_usage The image usage flags.
  /// @param aspect_flags The aspect flags.
  /// @param sample_count The sample count.
  /// @param name The internal debug marker name of the VkImage.
  /// @param image_extent The width and height of the image.
  Image(const Device& device, vk::Format format, vk::ImageUsageFlags image_usage,
    vk::ImageAspectFlags aspect_flags, vk::SampleCountFlagBits sample_count,
    const std::string& name, vk::Extent2D image_extent);

  Image(const Image&) = delete;
  Image(Image&&) noexcept;

  ~Image();

  Image& operator=(const Image&) = delete;
  Image& operator=(Image&&) = delete;

  [[nodiscard]] vk::Format image_format() const { return m_format; }

  [[nodiscard]] vk::ImageView image_view() const { return m_image_view; }

  [[nodiscard]] vk::Image get() const { return m_image; }
};
};
