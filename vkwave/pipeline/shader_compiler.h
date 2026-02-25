#pragma once

#include <vkwave/config.h>
#include <vkwave/core/registered.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{

class ShaderCompiler : public Tracked<ShaderCompiler>
{
  friend class Tracked<ShaderCompiler>;
public:
  struct Result
  {
    std::vector<uint32_t> spirv;
    std::string log; // warnings/errors
  };

  ~ShaderCompiler();

  void set_debug_info(bool enable) { m_debug_info = enable; }
  void set_optimization(bool enable) { m_optimize = enable; }

  /// Compile GLSL file to SPIR-V. Throws on failure.
  Result compile(const std::string& filepath,
    vk::ShaderStageFlagBits stage) const;

  /// Create VkShaderModule from compiled SPIR-V.
  static vk::ShaderModule create_module(vk::Device device,
    const std::vector<uint32_t>& spirv);

private:
  ShaderCompiler();

  bool m_debug_info{false};
  bool m_optimize{false};
};

} // namespace vkwave
