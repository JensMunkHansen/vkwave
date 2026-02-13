#include <vkwave/core/exception.h>
#include <vkwave/core/representation.h>

namespace vkwave
{

VulkanException::VulkanException(std::string message, const VkResult result)
  : SpsException(message.append(" (")
                   .append(vkwave::utils::as_string(result))
                   .append(": ")
                   .append(vkwave::utils::result_to_description(result))
                   .append(")"))
{
}

VulkanException::VulkanException(std::string message, const vk::Result result)
  : SpsException(message.append(" (")
                   .append(vk::to_string(result))
                   .append(": ")
                   .append(vkwave::utils::result_to_description(static_cast<VkResult>(result)))
                   .append(")"))
{
}

} // namespace vkwave
