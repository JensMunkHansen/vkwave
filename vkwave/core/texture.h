#pragma once

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>

namespace vkwave
{

class Device;

/// @brief RAII wrapper for Vulkan texture (image + view + sampler).
///
/// Handles creation and cleanup of:
/// - VkImage with device-local memory
/// - VkImageView for shader access
/// - VkSampler for texture filtering
///
/// Uses staging buffer for GPU upload with proper layout transitions.
class Texture
{
public:
  /// @brief Create texture from raw RGBA pixel data.
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the texture.
  /// @param pixels Pointer to RGBA8 pixel data.
  /// @param width Image width in pixels.
  /// @param height Image height in pixels.
  /// @param linear If true, use R8G8B8A8_UNORM (for normal/metallic/AO data).
  ///               If false (default), use R8G8B8A8_SRGB (for color textures).
  Texture(const Device& device, const std::string& name, const uint8_t* pixels, uint32_t width,
    uint32_t height, bool linear = false);

  /// @brief Create texture from file (PNG, JPEG, etc.).
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the texture.
  /// @param filepath Path to the image file.
  /// @param linear If true, use R8G8B8A8_UNORM. If false (default), use R8G8B8A8_SRGB.
  Texture(const Device& device, const std::string& name, const std::string& filepath,
    bool linear = false);

  ~Texture();

  // Non-copyable
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // Movable
  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;

  /// @brief Get the image view for descriptor binding.
  [[nodiscard]] vk::ImageView image_view() const { return m_image_view; }

  /// @brief Get the sampler for descriptor binding.
  [[nodiscard]] vk::Sampler sampler() const { return m_sampler; }

  /// @brief Get texture width.
  [[nodiscard]] uint32_t width() const { return m_width; }

  /// @brief Get texture height.
  [[nodiscard]] uint32_t height() const { return m_height; }

  /// @brief Get debug name.
  [[nodiscard]] const std::string& name() const { return m_name; }

private:
  const Device* m_device{ nullptr };
  std::string m_name;

  vk::Image m_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_memory{ VK_NULL_HANDLE };
  vk::ImageView m_image_view{ VK_NULL_HANDLE };
  vk::Sampler m_sampler{ VK_NULL_HANDLE };

  uint32_t m_width{ 0 };
  uint32_t m_height{ 0 };
  vk::Format m_format{ vk::Format::eR8G8B8A8Srgb };

  void create_image();
  void create_image_view();
  void create_sampler();
  void upload_pixels(const uint8_t* pixels);
  void transition_layout(vk::CommandBuffer cmd, vk::ImageLayout old_layout,
    vk::ImageLayout new_layout);
};

} // namespace vkwave
