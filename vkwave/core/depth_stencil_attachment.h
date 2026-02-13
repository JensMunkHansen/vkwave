#pragma once

#include <vulkan/vulkan.hpp>

namespace vkwave
{

class Device;

/// RAII wrapper for a depth-stencil image with separate aspect views.
///
/// Creates one VkImage with the requested depth(-stencil) format and up to
/// three ImageViews:
///   - combined_view(): depth+stencil aspects (for framebuffer attachment)
///   - depth_view():    depth-only aspect   (for sampling depth)
///   - stencil_view():  stencil-only aspect (for sampling stencil in compute)
///
/// The stencil view is only created when the format actually has a stencil
/// component (e.g. D32SfloatS8Uint, D24UnormS8Uint).
class DepthStencilAttachment
{
public:
  DepthStencilAttachment(const Device& device, vk::Format format,
    vk::Extent2D extent, vk::SampleCountFlagBits samples,
    vk::ImageUsageFlags extraUsage = {});
  ~DepthStencilAttachment();

  DepthStencilAttachment(const DepthStencilAttachment&) = delete;
  DepthStencilAttachment& operator=(const DepthStencilAttachment&) = delete;
  DepthStencilAttachment(DepthStencilAttachment&&) noexcept;
  DepthStencilAttachment& operator=(DepthStencilAttachment&&) noexcept;

  [[nodiscard]] vk::Image image() const { return m_image; }
  [[nodiscard]] vk::Format format() const { return m_format; }
  [[nodiscard]] vk::Extent2D extent() const { return m_extent; }
  [[nodiscard]] bool has_stencil() const;

  [[nodiscard]] vk::ImageView combined_view() const { return m_combinedView; }
  [[nodiscard]] vk::ImageView depth_view() const { return m_depthView; }
  [[nodiscard]] vk::ImageView stencil_view() const { return m_stencilView; }

private:
  void destroy();

  vk::Device m_vkDevice;
  vk::Image m_image;
  vk::DeviceMemory m_memory;
  vk::ImageView m_combinedView;
  vk::ImageView m_depthView;
  vk::ImageView m_stencilView;
  vk::Format m_format;
  vk::Extent2D m_extent;
};

} // namespace vkwave
