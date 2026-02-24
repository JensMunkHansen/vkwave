#include <vkwave/pipeline/shader_compiler.h>

#include <vkwave/core/instance.h>

#include <shaderc/shaderc.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace vkwave
{

// Derive shaderc targets from Instance::REQUIRED_VK_API_VERSION so
// the Vulkan version is defined in exactly one place.
static constexpr auto kVkApiVersion = Instance::REQUIRED_VK_API_VERSION;
static constexpr uint32_t kVkMinor = VK_API_VERSION_MINOR(kVkApiVersion);

static constexpr shaderc_env_version to_shaderc_env()
{
  if constexpr (kVkMinor >= 3) return shaderc_env_version_vulkan_1_3;
  if constexpr (kVkMinor >= 2) return shaderc_env_version_vulkan_1_2;
  if constexpr (kVkMinor >= 1) return shaderc_env_version_vulkan_1_1;
  return shaderc_env_version_vulkan_1_0;
}

static constexpr shaderc_spirv_version to_shaderc_spirv()
{
  if constexpr (kVkMinor >= 3) return shaderc_spirv_version_1_6;
  if constexpr (kVkMinor >= 2) return shaderc_spirv_version_1_5;
  if constexpr (kVkMinor >= 1) return shaderc_spirv_version_1_3;
  return shaderc_spirv_version_1_0;
}

static shaderc_shader_kind to_shaderc_kind(vk::ShaderStageFlagBits stage)
{
  switch (stage)
  {
  case vk::ShaderStageFlagBits::eVertex: return shaderc_vertex_shader;
  case vk::ShaderStageFlagBits::eFragment: return shaderc_fragment_shader;
  case vk::ShaderStageFlagBits::eCompute: return shaderc_compute_shader;
  case vk::ShaderStageFlagBits::eGeometry: return shaderc_geometry_shader;
  case vk::ShaderStageFlagBits::eTessellationControl: return shaderc_tess_control_shader;
  case vk::ShaderStageFlagBits::eTessellationEvaluation: return shaderc_tess_evaluation_shader;
  default:
    throw std::runtime_error("Unsupported shader stage for compilation");
  }
}

/// Resolve #include directives relative to the including file's directory.
class FileIncluder : public shaderc::CompileOptions::IncluderInterface
{
public:
  explicit FileIncluder(std::string base_dir)
    : m_base_dir(std::move(base_dir)) {}

  shaderc_include_result* GetInclude(
    const char* requested_source,
    shaderc_include_type /*type*/,
    const char* /*requesting_source*/,
    size_t /*include_depth*/) override
  {
    auto path = std::filesystem::path(m_base_dir) / requested_source;
    auto* result = new shaderc_include_result;

    std::ifstream file(path);
    if (!file.is_open())
    {
      m_error = "Failed to open include: " + path.string();
      result->source_name = "";
      result->source_name_length = 0;
      result->content = m_error.c_str();
      result->content_length = m_error.size();
      result->user_data = nullptr;
      return result;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    m_content = ss.str();
    m_name = path.string();

    result->source_name = m_name.c_str();
    result->source_name_length = m_name.size();
    result->content = m_content.c_str();
    result->content_length = m_content.size();
    result->user_data = nullptr;
    return result;
  }

  void ReleaseInclude(shaderc_include_result* data) override
  {
    delete data;
  }

private:
  std::string m_base_dir;
  std::string m_content;
  std::string m_name;
  std::string m_error;
};

ShaderCompiler::Result ShaderCompiler::compile(
  const std::string& filepath, vk::ShaderStageFlagBits stage)
{
  // Read GLSL source from file
  std::ifstream file(filepath);
  if (!file.is_open())
    throw std::runtime_error("Failed to open shader file: " + filepath);

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string source = ss.str();

  // Configure compiler
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetTargetEnvironment(shaderc_target_env_vulkan, to_shaderc_env());
  options.SetTargetSpirv(to_shaderc_spirv());

  // Enable #include resolution relative to the shader's directory
  auto shader_dir = std::filesystem::path(filepath).parent_path().string();
  options.SetIncluder(std::make_unique<FileIncluder>(shader_dir));

#ifdef NDEBUG
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
#else
  options.SetOptimizationLevel(shaderc_optimization_level_zero);
#endif

  // Always generate debug info — SPIRV-Reflect needs binding names
  // for descriptor lookup (stripped by performance optimization otherwise).
  options.SetGenerateDebugInfo();

  // Extract filename for error messages
  auto slash = filepath.find_last_of("/\\");
  std::string filename = (slash != std::string::npos)
    ? filepath.substr(slash + 1)
    : filepath;

  // Compile
  auto result = compiler.CompileGlslToSpv(
    source, to_shaderc_kind(stage), filename.c_str(), options);

  Result out;
  out.log = result.GetErrorMessage();

  if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    throw std::runtime_error("Shader compilation failed (" + filepath + "):\n" + out.log);

  out.spirv.assign(result.cbegin(), result.cend());
  return out;
}

vk::ShaderModule ShaderCompiler::create_module(
  vk::Device device, const std::vector<uint32_t>& spirv)
{
  vk::ShaderModuleCreateInfo info{};
  info.codeSize = spirv.size() * sizeof(uint32_t);
  info.pCode = spirv.data();
  return device.createShaderModule(info);
}

} // namespace vkwave
