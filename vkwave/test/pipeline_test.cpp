#include <catch2/catch_test_macros.hpp>

#include <vkwave/core/camera_ubo.h>
#include <vkwave/core/push_constants.h>
#include <vkwave/pipeline/shader_compiler.h>
#include <vkwave/pipeline/shader_reflection.h>

// --- Fullscreen shader tests ---

TEST_CASE("vkwave::pipeline::shader_compiler_compiles_vertex", "[pipeline]")
{
  auto result = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203); // SPIR-V magic number
}

TEST_CASE("vkwave::pipeline::shader_compiler_compiles_fragment", "[pipeline]")
{
  auto result = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.frag", vk::ShaderStageFlagBits::eFragment);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203); // SPIR-V magic number
}

TEST_CASE("vkwave::pipeline::reflection_extracts_push_constants", "[pipeline]")
{
  auto result = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(result.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  auto& ranges = reflection.push_constant_ranges();
  REQUIRE(ranges.size() == 1);
  CHECK(ranges[0].size == sizeof(vkwave::TrianglePushConstants));
  CHECK(ranges[0].stageFlags == vk::ShaderStageFlagBits::eFragment);
}

TEST_CASE("vkwave::pipeline::reflection_no_descriptors_for_fullscreen", "[pipeline]")
{
  auto vert = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.vert", vk::ShaderStageFlagBits::eVertex);
  auto frag = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  CHECK(reflection.descriptor_set_infos().empty());
}

TEST_CASE("vkwave::pipeline::reflection_validates_push_constant_size", "[pipeline]")
{
  auto frag = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "fullscreen.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  // Should not assert for correct size
  reflection.validate_push_constant_size(sizeof(vkwave::TrianglePushConstants));
}

// --- Cube shader tests ---

TEST_CASE("vkwave::pipeline::shader_compiler_compiles_cube_vertex", "[pipeline]")
{
  auto result = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203); // SPIR-V magic number
}

TEST_CASE("vkwave::pipeline::shader_compiler_compiles_cube_fragment", "[pipeline]")
{
  auto result = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.frag", vk::ShaderStageFlagBits::eFragment);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203); // SPIR-V magic number
}

TEST_CASE("vkwave::pipeline::reflection_extracts_cube_ubo", "[pipeline]")
{
  auto vert = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  auto frag = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  auto& sets = reflection.descriptor_set_infos();
  REQUIRE(sets.size() == 1);
  CHECK(sets[0].set == 0);
  REQUIRE(sets[0].bindings.size() == 1);
  CHECK(sets[0].bindings[0].binding == 0);
  CHECK(sets[0].bindings[0].type == vk::DescriptorType::eUniformBuffer);
  CHECK(sets[0].bindings[0].blockSize == sizeof(vkwave::CameraUBO));
}

TEST_CASE("vkwave::pipeline::reflection_extracts_cube_push_constants", "[pipeline]")
{
  auto vert = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  auto frag = vkwave::ShaderCompiler::compile(
    TEST_SHADER_DIR "cube.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  reflection.validate_push_constant_size(sizeof(vkwave::CubePushConstants));
}
