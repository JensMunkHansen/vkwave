#pragma once

#include <vkwave/config.h>

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{

class ShaderCompiler
{
public:
  struct Result
  {
    std::vector<uint32_t> spirv;
    std::string log; // warnings/errors
  };

  /// Compile GLSL file to SPIR-V. Throws on failure.
  static Result compile(const std::string& filepath,
    vk::ShaderStageFlagBits stage);

  /// Create VkShaderModule from compiled SPIR-V.
  static vk::ShaderModule create_module(vk::Device device,
    const std::vector<uint32_t>& spirv);
};

} // namespace vkwave
