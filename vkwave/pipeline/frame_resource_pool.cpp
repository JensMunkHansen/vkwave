#include <vkwave/pipeline/frame_resource_pool.h>

#include <vkwave/core/device.h>

#include <spdlog/fmt/fmt.h>

#include <cassert>

namespace vkwave
{

FrameResourcePool::ColorHandle FrameResourcePool::add_color(
  std::string name, vk::Format format, vk::ImageUsageFlags usage,
  vk::SampleCountFlagBits samples)
{
  m_color_specs.push_back({ std::move(name), format, usage, samples });
  return static_cast<ColorHandle>(m_color_specs.size() - 1);
}

FrameResourcePool::DepthHandle FrameResourcePool::add_depth(
  std::string name, vk::Format format, vk::SampleCountFlagBits samples,
  vk::ImageUsageFlags extra_usage)
{
  m_depth_specs.push_back({ std::move(name), format, samples, extra_usage });
  return static_cast<DepthHandle>(m_depth_specs.size() - 1);
}

void FrameResourcePool::create(
  const Device& device, vk::Extent2D extent, uint32_t count)
{
  m_extent = extent;
  m_count = count;

  m_color.clear();
  m_color.resize(m_color_specs.size());
  for (size_t h = 0; h < m_color_specs.size(); ++h)
  {
    const auto& spec = m_color_specs[h];
    m_color[h].reserve(count);
    for (uint32_t i = 0; i < count; ++i)
      m_color[h].emplace_back(device, spec.format, extent, spec.usage,
        fmt::format("{}_{}", spec.name, i), spec.samples);
  }

  m_depth.clear();
  m_depth.resize(m_depth_specs.size());
  for (size_t h = 0; h < m_depth_specs.size(); ++h)
  {
    const auto& spec = m_depth_specs[h];
    m_depth[h].reserve(count);
    for (uint32_t i = 0; i < count; ++i)
      m_depth[h].emplace_back(device, spec.format, extent, spec.samples,
        spec.extra_usage);
  }
}

void FrameResourcePool::destroy()
{
  m_color.clear();
  m_depth.clear();
  m_count = 0;
}

vk::ImageView FrameResourcePool::color_view(ColorHandle handle, uint32_t slot) const
{
  assert(handle < m_color.size() && slot < m_color[handle].size());
  return m_color[handle][slot].image_view();
}

vk::Image FrameResourcePool::color_image(ColorHandle handle, uint32_t slot) const
{
  assert(handle < m_color.size() && slot < m_color[handle].size());
  return m_color[handle][slot].image();
}

vk::Format FrameResourcePool::color_format(ColorHandle handle) const
{
  assert(handle < m_color_specs.size());
  return m_color_specs[handle].format;
}

vk::ImageView FrameResourcePool::depth_view(DepthHandle handle, uint32_t slot) const
{
  assert(handle < m_depth.size() && slot < m_depth[handle].size());
  return m_depth[handle][slot].combined_view();
}

vk::Format FrameResourcePool::depth_format(DepthHandle handle) const
{
  assert(handle < m_depth_specs.size());
  return m_depth_specs[handle].format;
}

} // namespace vkwave
