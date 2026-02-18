#include <vkwave/pipeline/shader_reflection.h>

#include <spirv_reflect.h>

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>

namespace vkwave
{

static vk::DescriptorType to_vk_descriptor_type(SpvReflectDescriptorType type)
{
  switch (type)
  {
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    return vk::DescriptorType::eSampler;
  case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    return vk::DescriptorType::eCombinedImageSampler;
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    return vk::DescriptorType::eSampledImage;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    return vk::DescriptorType::eStorageImage;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    return vk::DescriptorType::eUniformTexelBuffer;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
    return vk::DescriptorType::eStorageTexelBuffer;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    return vk::DescriptorType::eUniformBuffer;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    return vk::DescriptorType::eStorageBuffer;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    return vk::DescriptorType::eUniformBufferDynamic;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    return vk::DescriptorType::eStorageBufferDynamic;
  case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
    return vk::DescriptorType::eInputAttachment;
  default:
    throw std::runtime_error("Unknown SPIRV-Reflect descriptor type");
  }
}

void ShaderReflection::add_stage(
  const std::vector<uint32_t>& spirv, vk::ShaderStageFlagBits stage)
{
  SpvReflectShaderModule module{};
  SpvReflectResult result = spvReflectCreateShaderModule(
    spirv.size() * sizeof(uint32_t), spirv.data(), &module);
  if (result != SPV_REFLECT_RESULT_SUCCESS)
    throw std::runtime_error("spvReflectCreateShaderModule failed");

  // --- Push constants ---
  uint32_t pc_count = 0;
  spvReflectEnumeratePushConstantBlocks(&module, &pc_count, nullptr);
  if (pc_count > 0)
  {
    std::vector<SpvReflectBlockVariable*> pc_blocks(pc_count);
    spvReflectEnumeratePushConstantBlocks(&module, &pc_count, pc_blocks.data());

    for (uint32_t i = 0; i < pc_count; ++i)
    {
      vk::PushConstantRange range{};
      range.stageFlags = stage;
      range.offset = pc_blocks[i]->offset;
      range.size = pc_blocks[i]->size;
      push_constant_ranges_.push_back(range);
    }
  }

  // --- Descriptor bindings ---
  uint32_t binding_count = 0;
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);
  if (binding_count > 0)
  {
    std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
    spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings.data());

    for (uint32_t i = 0; i < binding_count; ++i)
    {
      auto* b = bindings[i];

      // Find or create the set entry
      uint32_t set_num = b->set;
      auto it = std::find_if(descriptor_sets_.begin(), descriptor_sets_.end(),
        [set_num](const DescriptorSetInfo& s) { return s.set == set_num; });

      if (it == descriptor_sets_.end())
      {
        descriptor_sets_.push_back({ set_num, {} });
        it = descriptor_sets_.end() - 1;
      }

      // Check if binding already exists (from another stage)
      auto bit = std::find_if(it->bindings.begin(), it->bindings.end(),
        [b](const DescriptorBindingInfo& bi) { return bi.binding == b->binding; });

      if (bit != it->bindings.end())
      {
        // Merge stage flags
        bit->stageFlags |= stage;
      }
      else
      {
        DescriptorBindingInfo info{};
        info.binding = b->binding;
        info.type = to_vk_descriptor_type(b->descriptor_type);
        info.count = b->count;
        info.stageFlags = stage;
        info.blockSize = (b->block.size > 0) ? b->block.size : 0;
        it->bindings.push_back(info);
      }
    }
  }

  spvReflectDestroyShaderModule(&module);
}

void ShaderReflection::finalize()
{
  // Merge push constant ranges that overlap (same offset) across stages
  // by combining their stageFlags
  std::vector<vk::PushConstantRange> merged;
  for (auto& range : push_constant_ranges_)
  {
    auto it = std::find_if(merged.begin(), merged.end(),
      [&](const vk::PushConstantRange& r) {
        return r.offset == range.offset && r.size == range.size;
      });
    if (it != merged.end())
      it->stageFlags |= range.stageFlags;
    else
      merged.push_back(range);
  }
  push_constant_ranges_ = std::move(merged);

  // Sort descriptor sets by set number, bindings by binding number
  std::sort(descriptor_sets_.begin(), descriptor_sets_.end(),
    [](const DescriptorSetInfo& a, const DescriptorSetInfo& b) {
      return a.set < b.set;
    });
  for (auto& set : descriptor_sets_)
  {
    std::sort(set.bindings.begin(), set.bindings.end(),
      [](const DescriptorBindingInfo& a, const DescriptorBindingInfo& b) {
        return a.binding < b.binding;
      });
  }
}

std::vector<vk::DescriptorSetLayout>
  ShaderReflection::create_descriptor_set_layouts(vk::Device device) const
{
  std::vector<vk::DescriptorSetLayout> layouts;
  layouts.reserve(descriptor_sets_.size());

  for (auto& set : descriptor_sets_)
  {
    std::vector<vk::DescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(set.bindings.size());

    for (auto& b : set.bindings)
    {
      vk::DescriptorSetLayoutBinding vk_b{};
      vk_b.binding = b.binding;
      vk_b.descriptorType = b.type;
      vk_b.descriptorCount = b.count;
      vk_b.stageFlags = b.stageFlags;
      vk_bindings.push_back(vk_b);
    }

    vk::DescriptorSetLayoutCreateInfo info{};
    info.bindingCount = static_cast<uint32_t>(vk_bindings.size());
    info.pBindings = vk_bindings.data();

    layouts.push_back(device.createDescriptorSetLayout(info));
  }

  return layouts;
}

void ShaderReflection::validate_push_constant_size(uint32_t expected) const
{
#ifndef NDEBUG
  uint32_t total = 0;
  for (auto& range : push_constant_ranges_)
    total = std::max(total, range.offset + range.size);
  assert(total == expected && "Push constant size mismatch between shader and C++ struct");
#else
  (void)expected;
#endif
}

void ShaderReflection::validate_ubo_size(
  uint32_t set, uint32_t binding, uint32_t expected) const
{
#ifndef NDEBUG
  for (auto& s : descriptor_sets_)
  {
    if (s.set != set) continue;
    for (auto& b : s.bindings)
    {
      if (b.binding != binding) continue;
      assert(b.blockSize == expected &&
        "UBO block size mismatch between shader and C++ struct");
      return;
    }
  }
  assert(false && "UBO binding not found in reflection data");
#else
  (void)set;
  (void)binding;
  (void)expected;
#endif
}

} // namespace vkwave
