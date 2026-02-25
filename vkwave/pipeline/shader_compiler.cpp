#include <vkwave/pipeline/shader_compiler.h>

#include <vkwave/core/instance.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace vkwave
{

// Derive glslang targets from Instance::REQUIRED_VK_API_VERSION so
// the Vulkan version is defined in exactly one place.
static constexpr auto kVkApiVersion = Instance::REQUIRED_VK_API_VERSION;
static constexpr uint32_t kVkMinor = VK_API_VERSION_MINOR(kVkApiVersion);

static constexpr glslang::EShTargetClientVersion to_glslang_client_version()
{
  if constexpr (kVkMinor >= 3) return glslang::EShTargetVulkan_1_3;
  if constexpr (kVkMinor >= 2) return glslang::EShTargetVulkan_1_2;
  if constexpr (kVkMinor >= 1) return glslang::EShTargetVulkan_1_1;
  return glslang::EShTargetVulkan_1_0;
}

static constexpr glslang::EShTargetLanguageVersion to_glslang_spirv_version()
{
  if constexpr (kVkMinor >= 3) return glslang::EShTargetSpv_1_6;
  if constexpr (kVkMinor >= 2) return glslang::EShTargetSpv_1_5;
  if constexpr (kVkMinor >= 1) return glslang::EShTargetSpv_1_3;
  return glslang::EShTargetSpv_1_0;
}

static EShLanguage to_glslang_stage(vk::ShaderStageFlagBits stage)
{
  switch (stage)
  {
  case vk::ShaderStageFlagBits::eVertex:                 return EShLangVertex;
  case vk::ShaderStageFlagBits::eFragment:               return EShLangFragment;
  case vk::ShaderStageFlagBits::eCompute:                return EShLangCompute;
  case vk::ShaderStageFlagBits::eGeometry:               return EShLangGeometry;
  case vk::ShaderStageFlagBits::eTessellationControl:    return EShLangTessControl;
  case vk::ShaderStageFlagBits::eTessellationEvaluation: return EShLangTessEvaluation;
  default:
    throw std::runtime_error("Unsupported shader stage for compilation");
  }
}

/// Resolve #include directives relative to the including file's directory.
class FileIncluder : public glslang::TShader::Includer
{
public:
  explicit FileIncluder(std::string base_dir)
    : m_base_dir(std::move(base_dir)) {}

  IncludeResult* includeLocal(
    const char* header_name,
    const char* includer_name,
    size_t /*inclusion_depth*/) override
  {
    // Resolve relative to the includer's directory, falling back to base dir
    std::filesystem::path dir;
    if (includer_name[0])
      dir = std::filesystem::path(includer_name).parent_path();
    if (dir.empty())
      dir = m_base_dir;
    auto path = dir / header_name;

    std::ifstream file(path);
    if (!file.is_open())
      return nullptr;

    std::ostringstream ss;
    ss << file.rdbuf();
    auto* content = new std::string(ss.str());
    auto resolved = path.string();

    return new IncludeResult(resolved, content->c_str(), content->size(), content);
  }

  IncludeResult* includeSystem(
    const char* header_name,
    const char* includer_name,
    size_t inclusion_depth) override
  {
    // Fall back to local include resolution for system includes
    return includeLocal(header_name, includer_name, inclusion_depth);
  }

  void releaseInclude(IncludeResult* result) override
  {
    if (result)
    {
      delete static_cast<std::string*>(result->userData);
      delete result;
    }
  }

private:
  std::string m_base_dir;
};

// glslang requires exactly one InitializeProcess() per process lifetime.
static std::once_flag g_glslang_init;
static bool g_glslang_initialized = false;

ShaderCompiler::ShaderCompiler()
{
  std::call_once(g_glslang_init, [] {
    glslang::InitializeProcess();
    g_glslang_initialized = true;
    spdlog::debug("glslang initialized");
  });
}

ShaderCompiler::~ShaderCompiler()
{
  // Don't finalize — other ShaderCompiler instances or late compilations
  // may still need glslang. Process-exit cleanup is fine.
}

ShaderCompiler::Result ShaderCompiler::compile(
  const std::string& filepath, vk::ShaderStageFlagBits stage) const
{
  // Read GLSL source from file
  std::ifstream file(filepath);
  if (!file.is_open())
    throw std::runtime_error("Failed to open shader file: " + filepath);

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string source = ss.str();

  // Extract filename for error messages
  auto slash = filepath.find_last_of("/\\");
  std::string filename = (slash != std::string::npos)
    ? filepath.substr(slash + 1)
    : filepath;

  // Set up glslang shader
  auto lang_stage = to_glslang_stage(stage);
  glslang::TShader shader(lang_stage);

  const char* source_cstr = source.c_str();
  const int source_len = static_cast<int>(source.size());
  const char* name_cstr = filename.c_str();
  shader.setStringsWithLengthsAndNames(&source_cstr, &source_len, &name_cstr, 1);
  shader.setEntryPoint("main");
  shader.setSourceEntryPoint("main");

  if (m_debug_info)
    shader.setDebugInfo(true);

  shader.setEnvInput(glslang::EShSourceGlsl, lang_stage,
    glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, to_glslang_client_version());
  shader.setEnvTarget(glslang::EShTargetSpv, to_glslang_spirv_version());

  // Parse messages — Vulkan + SPIR-V rules, optionally debug info
  auto messages = static_cast<EShMessages>(
    EShMsgSpvRules | EShMsgVulkanRules);
  if (m_debug_info)
    messages = static_cast<EShMessages>(messages | EShMsgDebugInfo);

  // Include resolution relative to the shader's directory
  auto shader_dir = std::filesystem::path(filepath).parent_path().string();
  FileIncluder includer(shader_dir);

  // Parse
  if (!shader.parse(GetDefaultResources(), 460, ENoProfile, false, false,
      messages, includer))
  {
    throw std::runtime_error(
      "Shader parsing failed (" + filepath + "):\n" + shader.getInfoLog());
  }

  // Link
  glslang::TProgram program;
  program.addShader(&shader);

  if (!program.link(messages))
  {
    throw std::runtime_error(
      "Shader linking failed (" + filepath + "):\n" + program.getInfoLog());
  }

  // Generate SPIR-V
  glslang::SpvOptions spv_options{};
  spv_options.generateDebugInfo = true; // Always — SPIRV-Reflect needs binding names
  spv_options.disableOptimizer = !m_optimize;

  if (m_debug_info)
  {
    spv_options.emitNonSemanticShaderDebugInfo = true;
    spv_options.emitNonSemanticShaderDebugSource = true;
  }

  Result out;
  spv::SpvBuildLogger spv_logger;
  glslang::GlslangToSpv(
    *program.getIntermediate(lang_stage), out.spirv,
    &spv_logger, &spv_options);

  out.log = spv_logger.getAllMessages();
  if (!out.log.empty())
    spdlog::debug("SPIR-V gen ({}): {}", filename, out.log);

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
