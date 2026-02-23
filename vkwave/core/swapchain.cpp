#include <vkwave/config.h>

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <vkwave/core/device.h>
#include <vkwave/core/exception.h>
#include <vkwave/core/representation.h>
#include <vkwave/core/swapchain.h>

#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <vulkan/vulkan_to_string.hpp>

namespace vkwave
{

std::optional<vk::CompositeAlphaFlagBitsKHR> Swapchain::choose_composite_alpha(
  const vk::CompositeAlphaFlagBitsKHR request_composite_alpha,
  const vk::CompositeAlphaFlagsKHR supported_composite_alpha)
{
  if (request_composite_alpha & supported_composite_alpha) // != 0u
  {
    return request_composite_alpha;
  }
  static const std::vector<vk::CompositeAlphaFlagBitsKHR> composite_alpha_flags{
    vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eOpaque,
    vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
    vk::CompositeAlphaFlagBitsKHR::eInherit
  };

  for (const auto flag : composite_alpha_flags)
  {
    if (flag & supported_composite_alpha) // != 0u
    {
      spdlog::trace("Swapchain composite alpha '{}' is not supported, selecting '{}'",
        utils::as_string(request_composite_alpha), utils::as_string(flag));
      return flag;
    }
  }
  return std::nullopt;
}

vk::Extent2D Swapchain::choose_image_extent(const vk::Extent2D& requested_extent,
  const vk::Extent2D& min_extent, const vk::Extent2D& max_extent,
  const vk::Extent2D& current_extent)
{
  if (current_extent.width == std::numeric_limits<std::uint32_t>::max())
  {
    return requested_extent;
  }
  if (requested_extent.width < 1 || requested_extent.height < 1)
  {
    spdlog::trace("Swapchain image extent ({}, {}) is not supported! Selecting ({}, {})",
      requested_extent.width, requested_extent.height, current_extent.width, current_extent.height);
    return current_extent;
  }
  vk::Extent2D extent;
  extent.width = std::clamp(requested_extent.width, min_extent.width, max_extent.width);
  extent.height = std::clamp(requested_extent.height, min_extent.height, max_extent.height);
  return extent;
}

vk::PresentModeKHR Swapchain::choose_present_mode(
  const std::vector<vk::PresentModeKHR>& available_present_modes,
  const std::vector<vk::PresentModeKHR>& present_mode_priority_list, const bool vsync_enabled)
{
  assert(!available_present_modes.empty());
  assert(!present_mode_priority_list.empty());

  vk::PresentModeKHR chosenPresentMode = vk::PresentModeKHR::eFifo;

  if (!vsync_enabled)
  {
    for (const auto requested_present_mode : present_mode_priority_list)
    {
      const auto present_mode = std::find(
        available_present_modes.begin(), available_present_modes.end(), requested_present_mode);
      if (present_mode != available_present_modes.end())
      {
        chosenPresentMode = *present_mode;
        break;
      }
    }
  }
  // chosenPresentMode = vk::PresentModeKHR::eImmediate;
  spdlog::trace("Selected present mode: {}", vk::to_string(chosenPresentMode));
  return chosenPresentMode;
}

std::optional<vk::SurfaceFormatKHR> Swapchain::choose_surface_format(
  const std::vector<vk::SurfaceFormatKHR>& available_formats,
  const std::vector<vk::SurfaceFormatKHR>& format_prioriy_list)
{
  assert(!available_formats.empty());

  // Try to find one of the formats in the priority list
  spdlog::trace("The format priority list has {} elements", format_prioriy_list.size());
  for (const auto requested_format : format_prioriy_list)
  {
    const auto format = std::find_if(available_formats.begin(), available_formats.end(),
      [&](const vk::SurfaceFormatKHR candidate)
      {
        return requested_format.format == candidate.format &&
          requested_format.colorSpace == candidate.colorSpace;
      });
    if (format != available_formats.end())
    {
      spdlog::trace("Selecting swapchain surface format {}", utils::as_string(*format));
      return *format;
    }
  }

  spdlog::trace("None of the surface formats of the priority list are supported");
  spdlog::trace("Selecting surface format from default list");

  // UNORM formats: shader does manual linearToSRGB(), no hardware auto-conversion.
  // sRGB formats: hardware auto-converts linear->sRGB on write — would DOUBLE-gamma
  // with our manual conversion. Always prefer UNORM.
  static const std::vector<vk::SurfaceFormatKHR> default_surface_format_priority_list{
    { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear },
    { vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear },
    { vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
    { vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
  };

  // Iterate the priority list (highest priority first), return first available match
  for (const auto& preferred : default_surface_format_priority_list)
  {
    const auto it = std::find_if(available_formats.begin(), available_formats.end(),
      [&](const vk::SurfaceFormatKHR candidate)
      {
        return preferred.format == candidate.format &&
          preferred.colorSpace == candidate.colorSpace;
      });

    if (it != available_formats.end())
    {
      spdlog::trace("Selecting swapchain image format {}", utils::as_string(*it));
      return *it;
    }
  }

  // No match — return first available as last resort
  spdlog::warn("No preferred swapchain format found, using first available: {}",
    utils::as_string(available_formats.front()));
  return available_formats.front();
}

std::vector<vk::Image> Swapchain::get_swapchain_images()
{
  return m_device.device().getSwapchainImagesKHR(m_swapchain);
}

void Swapchain::setup_swapchain(
  const std::uint32_t width, const std::uint32_t height, const bool vsync_enabled)
{
  const auto caps = m_device.surfaceCapabilities(m_surface);

  if (spdlog::get_level() == spdlog::level::trace)
  {
    spdlog::trace("Swapchain can support the following surface capabilities:");

    spdlog::trace("\tminimum image count: {}", caps.minImageCount);
    spdlog::trace("\tmaximum image count: {}", caps.maxImageCount);

    spdlog::trace("\tcurrent extent: ");
    spdlog::trace("\t\twidth: {}", caps.currentExtent.width);
    spdlog::trace("\t\theight: {}", caps.currentExtent.height);

    spdlog::trace("\tminimum supported extent: ");
    spdlog::trace("\t\twidth: {}", caps.minImageExtent.width);
    spdlog::trace("\t\theight: {}", caps.minImageExtent.height);

    spdlog::trace("\tmaximum supported extent: ");
    spdlog::trace("\t\twidth: {}", caps.maxImageExtent.width);
    spdlog::trace("\t\theight: {}", caps.maxImageExtent.height);

    spdlog::trace("\tmaximum image array layers: {}", caps.maxImageArrayLayers);

    spdlog::trace("\tsupported transforms:");
    std::vector<std::string> stringList = utils::as_description(caps.supportedTransforms);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t {}", line);
    }

    spdlog::trace("\tcurrent transform:");

    stringList = utils::as_description(caps.currentTransform);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t {}", line);
    }

    spdlog::trace("\tsupported alpha operations:");
    stringList = utils::log_alpha_composite_bits(caps.supportedCompositeAlpha);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t{}", line);
    }

    spdlog::trace("\tsupported image usage:");
    stringList = utils::log_image_usage_bits(caps.supportedUsageFlags);
    for (std::string line : stringList)
    {
      spdlog::trace("\t\t{}", line);
    }
  }

  std::vector<vk::SurfaceFormatKHR> formats =
    m_device.physicalDevice().getSurfaceFormatsKHR(m_surface);

  spdlog::trace("supported surface format");
  for (vk::SurfaceFormatKHR supportedFormat : formats)
  {
    spdlog::trace("\tpixel format: {}\tcolor space: {}", vk::to_string(supportedFormat.format),
      vk::to_string(supportedFormat.colorSpace));
  }

  // m_surface_format = vkwave::choose_swapchain_surface_format(formats);
  m_surface_format = choose_surface_format(formats);

  if (m_surface_format)
  {
    spdlog::info("Selected swapchain format: {} ({})",
      vk::to_string(m_surface_format->format), vk::to_string(m_surface_format->colorSpace));

    // Warn if we ended up with an sRGB format (shader does manual gamma)
    const auto fmt = m_surface_format->format;
    if (fmt == vk::Format::eB8G8R8A8Srgb || fmt == vk::Format::eR8G8B8A8Srgb)
    {
      spdlog::warn("sRGB swapchain format selected — shader does manual linearToSRGB(), "
        "this will cause double gamma correction!");
    }
  }

  const vk::Extent2D requested_extent{ width, height };

  // Display the supported present modes
  auto presentModes = m_device.physicalDevice().getSurfacePresentModesKHR(m_surface);
  spdlog::trace("supported present modes");
  for (vk::PresentModeKHR presentMode : presentModes)
  {
    spdlog::trace("\t {}", utils::log_present_mode(presentMode));
  }

  static const std::vector<vk::PresentModeKHR> default_present_mode_priorities{
    vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eMailbox,
    vk::PresentModeKHR::eFifoRelaxed, vk::PresentModeKHR::eFifo
  };

  const auto composite_alpha =
    choose_composite_alpha(vk::CompositeAlphaFlagBitsKHR::eOpaque, caps.supportedCompositeAlpha);

  if (!composite_alpha)
  {
    throw std::runtime_error("Error: Could not find suitable composite alpha!");
  }

  if ((caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) ==
    static_cast<vk::ImageUsageFlagBits>(0u))
  {
    throw std::runtime_error("Error: Swapchain image usage flag bit "
                             "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT is not supported!");
  }

  vk::SwapchainKHR old_swapchain = m_swapchain;

  uint32_t imageCount = (m_preferred_image_count > 0)
    ? std::max(m_preferred_image_count, caps.minImageCount)
    : std::max(caps.minImageCount + 1, caps.minImageCount);
  if (caps.maxImageCount != 0)
    imageCount = std::min(imageCount, caps.maxImageCount);
  spdlog::info("Swapchain image count: {} (min={}, max={}, requested={})",
    imageCount, caps.minImageCount, caps.maxImageCount, m_preferred_image_count);

  // Choose the actual extent (may differ from requested due to surface constraints)
  const vk::Extent2D chosen_extent = choose_image_extent(
    requested_extent, caps.minImageExtent, caps.maxImageExtent, caps.currentExtent);

  // Usage flags: color attachment for raster, transfer dst for RT blit, transfer src for screenshots
  vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
  if (caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)
  {
    imageUsage |= vk::ImageUsageFlagBits::eTransferDst;
  }
  if (caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
  {
    imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
  }

  vk::SwapchainCreateInfoKHR createInfo =
    vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(),      //
      m_surface,                                                   //
      imageCount,                                                  //
      m_surface_format.value().format,                             //
      m_surface_format.value().colorSpace,                         //
      chosen_extent,                                               //
      1, // Image array layers
      imageUsage);

  if (m_device.m_present_queue_family_index != m_device.m_graphics_queue_family_index)
  {
    createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = 2;
    uint32_t queueFamilyIndices[] = { m_device.m_graphics_queue_family_index,
      m_device.m_present_queue_family_index };
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  }
  else
  {
    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
  }

  createInfo.preTransform = (vk::SurfaceTransformFlagBitsKHR::eIdentity & caps.supportedTransforms)
    ? vk::SurfaceTransformFlagBitsKHR::eIdentity
    : caps.currentTransform;

  createInfo.compositeAlpha = composite_alpha.value();
  createInfo.clipped = VK_TRUE;

  // Present modes
  if (m_preferred_present_mode.has_value())
  {
    auto available = m_device.physicalDevice().getSurfacePresentModesKHR(m_surface);
    auto it = std::find(available.begin(), available.end(), *m_preferred_present_mode);
    if (it != available.end())
    {
      createInfo.presentMode = *m_preferred_present_mode;
      spdlog::info("Using preferred present mode: {}", vk::to_string(createInfo.presentMode));
    }
    else
    {
      createInfo.presentMode =
        choose_present_mode(available, default_present_mode_priorities, vsync_enabled);
      spdlog::warn("Preferred present mode {} not available, falling back to {}",
        vk::to_string(*m_preferred_present_mode), vk::to_string(createInfo.presentMode));
    }
  }
  else
  {
    createInfo.presentMode =
      choose_present_mode(m_device.physicalDevice().getSurfacePresentModesKHR(m_surface),
        default_present_mode_priorities, vsync_enabled);
  }

  createInfo.oldSwapchain = old_swapchain;

  m_present_mode = createInfo.presentMode;

  spdlog::trace("Using swapchain surface transform {}", vk::to_string(createInfo.preTransform));

  spdlog::trace("Creating swapchain");

  try
  {
    m_swapchain = m_device.device().createSwapchainKHR(createInfo);
  }
  catch (vk::SystemError err)
  {
    throw std::runtime_error("failed to create swap chain!");
  }

  if (old_swapchain != vk::SwapchainKHR(nullptr))
  {
    for (auto const img_view : m_img_views)
    {
      // An image view for each frame
      m_device.device().destroyImageView(img_view);
    }
    m_imgs.clear();
    m_img_views.clear();
    m_device.device().destroySwapchainKHR(old_swapchain);
  }

  // Store the ACTUAL chosen extent, not the requested extent
  m_extent = chosen_extent;
  spdlog::trace("Swapchain created with extent {}x{} (requested {}x{})",
    m_extent.width, m_extent.height, width, height);

  m_imgs = m_device.device().getSwapchainImagesKHR(m_swapchain);
  if (m_imgs.empty())
  {
    throw std::runtime_error("Error: Swapchain image count is 0!");
  }

  spdlog::trace("Creating {} swapchain image views", m_imgs.size());

  m_img_views.resize(m_imgs.size());

  for (std::size_t i = 0; i < m_imgs.size(); i++)
  {
    vk::ImageViewCreateInfo createInfo = {};
    createInfo.image = m_imgs[i];
    createInfo.viewType = vk::ImageViewType::e2D;
    createInfo.format = m_surface_format.value().format;
    createInfo.components.r = vk::ComponentSwizzle::eIdentity;
    createInfo.components.g = vk::ComponentSwizzle::eIdentity;
    createInfo.components.b = vk::ComponentSwizzle::eIdentity;
    createInfo.components.a = vk::ComponentSwizzle::eIdentity;
    createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    m_device.create_image_view(createInfo, &m_img_views[i], "swapchain image view");
  }
}

Swapchain::Swapchain(Device& device, const VkSurfaceKHR surface, const std::uint32_t width,
  const std::uint32_t height, const bool vsync_enabled,
  std::optional<vk::PresentModeKHR> preferred_present_mode,
  uint32_t preferred_image_count)
  : m_device(device)
  , m_surface(surface)
  , m_vsync_enabled(vsync_enabled)
  , m_preferred_present_mode(preferred_present_mode)
  , m_preferred_image_count(preferred_image_count)
{
  setup_swapchain(width, height, vsync_enabled);
}

Swapchain::Swapchain(Swapchain&& other) noexcept
  : m_device(other.m_device)
{
  m_swapchain = std::exchange(other.m_swapchain, VK_NULL_HANDLE);
  m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
  m_surface_format = other.m_surface_format;
  m_imgs = std::move(other.m_imgs);
  m_img_views = std::move(other.m_img_views);
  m_extent = other.m_extent;
  // Consider adding frame with sets of semaphores
  m_vsync_enabled = other.m_vsync_enabled;
  m_present_mode = other.m_present_mode;
}

Swapchain::~Swapchain()
{
  m_device.device().destroySwapchainKHR(m_swapchain);
  for (auto const img_view : m_img_views)
  {
    m_device.device().destroyImageView(img_view);
  }
}
}
