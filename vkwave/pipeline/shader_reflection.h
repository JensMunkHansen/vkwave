#pragma once

#include <vkwave/config.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkwave
{

/// Information about a single descriptor binding within a set.
struct DescriptorBindingInfo
{
  uint32_t binding;
  vk::DescriptorType type;
  uint32_t count;
  vk::ShaderStageFlags stageFlags;
  uint32_t blockSize; // for UBOs/SSBOs, 0 otherwise
  std::string name;   // GLSL variable name (from SPIR-V reflection)
};

/// Information about a descriptor set (all its bindings).
struct DescriptorSetInfo
{
  uint32_t set;
  std::vector<DescriptorBindingInfo> bindings;
};

class ShaderReflection
{
public:
  /// Add a compiled SPIR-V stage for reflection.
  void add_stage(const std::vector<uint32_t>& spirv,
    vk::ShaderStageFlagBits stage);

  /// Merge cross-stage bindings (must call after all add_stage calls).
  void finalize();

  /// Create descriptor set layouts from reflected data.
  std::vector<vk::DescriptorSetLayout>
    create_descriptor_set_layouts(vk::Device device) const;

  const std::vector<vk::PushConstantRange>& push_constant_ranges() const
  {
    return push_constant_ranges_;
  }

  const std::vector<DescriptorSetInfo>& descriptor_set_infos() const
  {
    return descriptor_sets_;
  }

  /// Debug validation: assert push constant total size matches expected.
  void validate_push_constant_size(uint32_t expected) const;

  /// Debug validation: assert UBO block size at (set, binding) matches expected.
  void validate_ubo_size(uint32_t set, uint32_t binding, uint32_t expected) const;

private:
  std::vector<vk::PushConstantRange> push_constant_ranges_;
  std::vector<DescriptorSetInfo> descriptor_sets_;
};

} // namespace vkwave
