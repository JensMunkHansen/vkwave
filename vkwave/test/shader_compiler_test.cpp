#include <catch2/catch_test_macros.hpp>

#include <vkwave/core/camera_ubo.h>
#include <vkwave/core/push_constants.h>
#include <vkwave/pipeline/shader_compiler.h>
#include <vkwave/pipeline/shader_reflection.h>

static auto g_compiler = vkwave::ShaderCompiler::create();

// --- Compile: debug x optimization matrix ---

TEST_CASE("vkwave::shader::compile_no_debug_no_opt", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  compiler->set_debug_info(false);
  compiler->set_optimization(false);
  auto result = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203);
}

TEST_CASE("vkwave::shader::compile_debug_no_opt", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  compiler->set_debug_info(true);
  compiler->set_optimization(false);
  auto result = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203);
  compiler->set_debug_info(false);
}

TEST_CASE("vkwave::shader::compile_no_debug_opt", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  compiler->set_debug_info(false);
  compiler->set_optimization(true);
  auto result = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203);
  compiler->set_optimization(false);
}

TEST_CASE("vkwave::shader::compile_debug_and_opt", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  compiler->set_debug_info(true);
  compiler->set_optimization(true);
  auto result = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  CHECK(!result.spirv.empty());
  CHECK(result.spirv[0] == 0x07230203);
  compiler->set_debug_info(false);
  compiler->set_optimization(false);
}

// --- Reflection: validation gated by debug flag ---

TEST_CASE("vkwave::shader::reflection_validate_skips_when_debug_off", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  auto vert = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  auto frag = compiler->compile(
    TEST_SHADER_DIR "cube.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  // Wrong sizes, but debug is off — should not throw
  CHECK_NOTHROW(reflection.validate_push_constant_size(999));
  CHECK_NOTHROW(reflection.validate_ubo_size(0, 0, 999));
}

TEST_CASE("vkwave::shader::reflection_validate_throws_when_debug_on", "[shader]")
{
  auto compiler = vkwave::ShaderCompiler::get();
  auto vert = compiler->compile(
    TEST_SHADER_DIR "cube.vert", vk::ShaderStageFlagBits::eVertex);
  auto frag = compiler->compile(
    TEST_SHADER_DIR "cube.frag", vk::ShaderStageFlagBits::eFragment);

  vkwave::ShaderReflection reflection;
  reflection.set_debug(true);
  reflection.add_stage(vert.spirv, vk::ShaderStageFlagBits::eVertex);
  reflection.add_stage(frag.spirv, vk::ShaderStageFlagBits::eFragment);
  reflection.finalize();

  // Correct sizes — should not throw
  CHECK_NOTHROW(
    reflection.validate_push_constant_size(sizeof(vkwave::CubePushConstants)));
  CHECK_NOTHROW(
    reflection.validate_ubo_size(0, 0, sizeof(vkwave::CameraUBO)));

  // Wrong sizes — should throw
  CHECK_THROWS_AS(reflection.validate_push_constant_size(999), std::runtime_error);
  CHECK_THROWS_AS(reflection.validate_ubo_size(0, 0, 999), std::runtime_error);
}
