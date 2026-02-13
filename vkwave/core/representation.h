#pragma once

#include <string_view>
#include <vulkan/vulkan.hpp>

#include <string>

namespace vkwave::utils
{

/// @brief This function returns a textual representation of the vulkan object T.
template <typename T>
[[nodiscard]] std::string_view as_string(T);

template <typename T>
[[nodiscard]] std::vector<std::string> as_description(T);

/// Get a feature description of a ``VkBool32`` value in the ``VkPhysicalDeviceFeatures`` struct by
/// index.
/// @param index The index of the ``VkBool32`` value in the ``VkPhysicalDeviceFeatures`` struct.
/// @note If the index is out of bounds, no exception will be thrown, but an empty description will
/// be returned instead.
/// @return A feature description
[[nodiscard]] std::string_view get_device_feature_description(std::size_t index);

/// @brief Convert a VkResult value into the corresponding error description as std::string_view
/// @param result The VkResult to convert
/// @return A std::string_view which contains an error description text of the VkResult
/// @note This function converts the VkResult into the corresponding error description text
/// If you want to convert it into an std::string_view, see the matching ```as_string``` template
[[nodiscard]] std::string_view result_to_description(VkResult result);

[[nodiscard]] std::string_view result_to_description(vk::Result result);

std::vector<std::string> log_alpha_composite_bits(vk::CompositeAlphaFlagsKHR bits);

std::vector<std::string> log_image_usage_bits(vk::ImageUsageFlags bits);

std::string log_present_mode(vk::PresentModeKHR presentMode);

} // namespace vkwave::utils
