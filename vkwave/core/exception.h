#pragma once

#include <vulkan/vulkan.hpp>

#include <stdexcept>
#include <string>

namespace vkwave
{

/// @brief A custom base class for exceptions
class SpsException : public std::runtime_error
{
public:
  // No need to define own constructors.
  using std::runtime_error::runtime_error;
};

/// @brief InexorException for Vulkan specific things.
class VulkanException final : public SpsException
{
public:
  /// @param message The exception message.
  /// @param result The VkResult value of the Vulkan API call which failed.
  VulkanException(std::string message, VkResult result);
  /// @param message The exception message.
  /// @param result The vk::Result value of the Vulkan API call which failed.
  VulkanException(std::string message, vk::Result result);
};

} // namespace vkwave
