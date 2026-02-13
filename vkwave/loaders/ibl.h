#pragma once

#include <vulkan/vulkan.hpp>
#include <string>

namespace vkwave
{

class Device;
class Texture;

/// @brief IBL compute shader generation settings
struct IBLSettings
{
  uint32_t resolution{ 256 };
  uint32_t irradiance_samples{ 2048 };
  uint32_t prefilter_samples{ 2048 };
  uint32_t brdf_samples{ 1024 };
};

/// @brief Image-Based Lighting (IBL) resources
/// Contains pre-computed environment maps for PBR rendering:
/// - BRDF LUT: 2D lookup table for split-sum approximation
/// - Irradiance cubemap: diffuse ambient lighting
/// - Pre-filtered environment cubemap: specular reflections (mip levels = roughness)
class IBL
{
public:
  /// @brief Create IBL resources
  /// @param device Vulkan device
  /// @param hdr_path Path to HDR environment map (equirectangular format)
  /// @param settings IBL generation settings (resolution, sample counts)
  IBL(const Device& device, const std::string& hdr_path, const IBLSettings& settings = {});

  /// @brief Create IBL with default neutral environment (for testing)
  explicit IBL(const Device& device);

  ~IBL();

  // Non-copyable
  IBL(const IBL&) = delete;
  IBL& operator=(const IBL&) = delete;

  // Accessors for descriptor binding
  [[nodiscard]] vk::ImageView brdf_lut_view() const { return m_brdf_lut_view; }
  [[nodiscard]] vk::Sampler brdf_lut_sampler() const { return m_brdf_lut_sampler; }

  [[nodiscard]] vk::ImageView irradiance_view() const { return m_irradiance_view; }
  [[nodiscard]] vk::Sampler irradiance_sampler() const { return m_irradiance_sampler; }

  [[nodiscard]] vk::ImageView prefiltered_view() const { return m_prefiltered_view; }
  [[nodiscard]] vk::Sampler prefiltered_sampler() const { return m_prefiltered_sampler; }

  [[nodiscard]] uint32_t mip_levels() const { return m_mip_levels; }
  [[nodiscard]] float intensity() const { return m_intensity; }
  void set_intensity(float intensity) { m_intensity = intensity; }

private:
  void load_hdr_environment(const std::string& hdr_path);
  void upload_hdr_to_gpu();
  void create_ibl_images();
  void run_compute_generation();
  void create_default_environment();

  const Device& m_device;
  IBLSettings m_settings;
  uint32_t m_resolution;
  uint32_t m_mip_levels;
  float m_intensity{ 1.0f };

  // BRDF LUT (2D texture)
  vk::Image m_brdf_lut_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_brdf_lut_memory{ VK_NULL_HANDLE };
  vk::ImageView m_brdf_lut_view{ VK_NULL_HANDLE };
  vk::Sampler m_brdf_lut_sampler{ VK_NULL_HANDLE };

  // Irradiance cubemap (diffuse IBL)
  vk::Image m_irradiance_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_irradiance_memory{ VK_NULL_HANDLE };
  vk::ImageView m_irradiance_view{ VK_NULL_HANDLE };
  vk::Sampler m_irradiance_sampler{ VK_NULL_HANDLE };

  // Pre-filtered environment cubemap (specular IBL)
  vk::Image m_prefiltered_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_prefiltered_memory{ VK_NULL_HANDLE };
  vk::ImageView m_prefiltered_view{ VK_NULL_HANDLE };
  vk::Sampler m_prefiltered_sampler{ VK_NULL_HANDLE };

  // Source HDR environment (equirectangular, GPU texture)
  vk::Image m_hdr_image{ VK_NULL_HANDLE };
  vk::DeviceMemory m_hdr_memory{ VK_NULL_HANDLE };
  vk::ImageView m_hdr_view{ VK_NULL_HANDLE };
  vk::Sampler m_hdr_sampler{ VK_NULL_HANDLE };

  // CPU-side HDR data for upload
  std::vector<float> m_hdr_data;
  uint32_t m_hdr_width{ 0 };
  uint32_t m_hdr_height{ 0 };
};

} // namespace vkwave
