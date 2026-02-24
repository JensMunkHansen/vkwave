#include "scene_data.h"

#include <vkwave/core/device.h>

#include <spdlog/spdlog.h>

#include <filesystem>

const vkwave::Mesh* SceneData::active_mesh() const
{
  return gltf_scene.mesh
    ? gltf_scene.mesh.get()
    : (gltf_model.mesh ? gltf_model.mesh.get() : cube_mesh.get());
}

bool SceneData::has_multi_material() const
{
  return gltf_scene.mesh && !gltf_scene.materials.empty();
}

uint32_t SceneData::material_count() const
{
  return has_multi_material()
    ? static_cast<uint32_t>(gltf_scene.materials.size()) : 1;
}

void SceneData::load_model(const vkwave::Device& device, const std::string& path)
{
  gltf_scene = {};
  gltf_model = {};
  cube_mesh.reset();

  if (!path.empty() && std::filesystem::exists(path))
  {
    spdlog::info("Loading glTF scene: {}", path);
    gltf_scene = vkwave::load_gltf_scene(device, path);
    if (!gltf_scene.mesh)
    {
      spdlog::warn("Scene load returned no mesh, falling back to single-material loader");
      gltf_model = vkwave::load_gltf_model(device, path);
    }
  }

  if (!gltf_scene.mesh && !gltf_model.mesh)
  {
    spdlog::info("Using default cube mesh");
    cube_mesh = vkwave::Mesh::create_cube(device);
  }
}

void SceneData::load_ibl(const vkwave::Device& device, const std::string& path)
{
  if (path.empty() || !std::filesystem::exists(path))
  {
    if (!path.empty())
      spdlog::warn("HDR file not found: {} -- using neutral IBL", path);
    ibl = std::make_unique<vkwave::IBL>(device);
  }
  else
  {
    spdlog::info("Loading HDR environment: {}", path);
    ibl = std::make_unique<vkwave::IBL>(device, path);
  }
}

void SceneData::create_fallback_textures(const vkwave::Device& device)
{
  const uint8_t white[] = { 255, 255, 255, 255 };
  fallback_white = std::make_unique<vkwave::Texture>(
    device, "fallback_white", white, 1, 1, false);

  const uint8_t flat_normal[] = { 128, 128, 255, 255 };
  fallback_normal = std::make_unique<vkwave::Texture>(
    device, "fallback_normal", flat_normal, 1, 1, true);

  const uint8_t default_mr[] = { 0, 128, 0, 255 };
  fallback_mr = std::make_unique<vkwave::Texture>(
    device, "fallback_mr", default_mr, 1, 1, true);

  const uint8_t black[] = { 0, 0, 0, 255 };
  fallback_black = std::make_unique<vkwave::Texture>(
    device, "fallback_black", black, 1, 1, false);
}
