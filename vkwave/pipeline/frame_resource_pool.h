#pragma once

#include <vkwave/core/depth_stencil_attachment.h>
#include <vkwave/core/image.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vkwave
{

class Device;

/// Graph-owned pool of ring-buffered (per-slot) render resources.
///
/// The render graph owns one of these and (re)creates it across
/// build()/resize(); passes reference its resources by handle + slot rather
/// than owning the images themselves. This keeps GPU-resource ownership in the
/// graph (Requirement #3) and lets multiple passes share a resource — e.g. an
/// opaque pass and a transmission pass depth-testing the same depth buffer.
///
/// Resources are *declared* once via add_color()/add_depth() (returning a
/// stable handle) and *allocated* per slot on create(). Handles stay valid
/// across destroy()/create() cycles, so registration survives a resize.
class FrameResourcePool
{
public:
  using ColorHandle = uint32_t;
  using DepthHandle = uint32_t;

  FrameResourcePool() = default;

  // Move-only: it owns move-only GPU resources (Image, DepthStencilAttachment),
  // so copying is forbidden — a copy would alias the same VkImage handles and
  // break the single-owner / per-slot-independence guarantee the overlap design
  // relies on. Explicitly spelled out (it would also be implicitly deleted).
  FrameResourcePool(const FrameResourcePool&) = delete;
  FrameResourcePool& operator=(const FrameResourcePool&) = delete;
  FrameResourcePool(FrameResourcePool&&) = default;
  FrameResourcePool& operator=(FrameResourcePool&&) = default;

  /// Register a per-slot color image (e.g. an HDR render target). Declared
  /// once; allocated on create(). Returns a stable handle.
  /// @param full_mips allocate a full mip chain (for roughness-blurred sampling,
  ///                  e.g. the transmission snapshot). Mip count is derived from
  ///                  the extent at create()/recreate() time.
  ColorHandle add_color(std::string name, vk::Format format,
    vk::ImageUsageFlags usage,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
    bool full_mips = false);

  /// Register a per-slot depth(-stencil) attachment.
  DepthHandle add_depth(std::string name, vk::Format format,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
    vk::ImageUsageFlags extra_usage = {});

  /// (Re)create all registered resources at the given extent and slot count.
  void create(const Device& device, vk::Extent2D extent, uint32_t count);

  /// Re-allocate all resources at the current extent/slot count — e.g. after a
  /// depth sample-count change. The GPU must be idle. No-op before create().
  void recreate(const Device& device);

  /// Update a depth resource's MSAA sample count (must match the consuming
  /// group's MSAA). Takes effect on the next create()/recreate().
  void set_depth_samples(DepthHandle handle, vk::SampleCountFlagBits samples);

  /// Destroy all per-slot resources. Registration (specs/handles) is retained
  /// so a subsequent create() re-allocates the same set.
  void destroy();

  /// Destroy resources AND drop all registrations (specs/handles). Use when the
  /// graph structure itself changes (e.g. a model switch adds/removes the
  /// transmission snapshot) and the pool must be re-registered from scratch.
  /// Invalidates every previously returned handle.
  void clear_specs();

  [[nodiscard]] vk::ImageView color_view(ColorHandle handle, uint32_t slot) const;
  [[nodiscard]] vk::Image color_image(ColorHandle handle, uint32_t slot) const;
  [[nodiscard]] vk::Format color_format(ColorHandle handle) const;
  [[nodiscard]] uint32_t color_mip_levels(ColorHandle handle, uint32_t slot) const;

  /// Depth attachment view (combined depth+stencil aspect, for framebuffers).
  [[nodiscard]] vk::ImageView depth_view(DepthHandle handle, uint32_t slot) const;
  [[nodiscard]] vk::Format depth_format(DepthHandle handle) const;

  [[nodiscard]] vk::Extent2D extent() const { return m_extent; }
  [[nodiscard]] uint32_t slot_count() const { return m_count; }

private:
  struct ColorSpec
  {
    std::string name;
    vk::Format format;
    vk::ImageUsageFlags usage;
    vk::SampleCountFlagBits samples;
    bool full_mips;
  };
  struct DepthSpec
  {
    std::string name;
    vk::Format format;
    vk::SampleCountFlagBits samples;
    vk::ImageUsageFlags extra_usage;
  };

  std::vector<ColorSpec> m_color_specs;
  std::vector<DepthSpec> m_depth_specs;

  std::vector<std::vector<Image>> m_color;                 // [handle][slot]
  std::vector<std::vector<DepthStencilAttachment>> m_depth; // [handle][slot]

  vk::Extent2D m_extent{};
  uint32_t m_count{ 0 };
};

} // namespace vkwave
