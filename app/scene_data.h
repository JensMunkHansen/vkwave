#pragma once

#include <vkwave/core/camera.h>
#include <vkwave/core/mesh.h>
#include <vkwave/core/texture.h>
#include <vkwave/loaders/gltf_loader.h>
#include <vkwave/loaders/ibl.h>

#include <vulkan/vulkan.hpp>

#include <memory>
#include <string>

namespace vkwave { class Device; }

/// Scene assets: geometry, materials, textures, IBL, camera.
/// Survives pipeline rebuilds (MSAA changes, resize).
/// Default-constructed empty -- call load_model / load_ibl / create_fallback_textures
/// explicitly before use.
struct SceneData
{
  vkwave::Camera camera;

  // PBR model + environment
  vkwave::GltfScene gltf_scene;
  vkwave::GltfModel gltf_model;             // legacy single-material fallback
  std::unique_ptr<vkwave::Mesh> cube_mesh;  // fallback when no model_path
  std::unique_ptr<vkwave::IBL> ibl;

  // Fallback 1x1 textures for missing material slots
  std::unique_ptr<vkwave::Texture> fallback_white;
  std::unique_ptr<vkwave::Texture> fallback_normal;
  std::unique_ptr<vkwave::Texture> fallback_mr;
  std::unique_ptr<vkwave::Texture> fallback_black;

  // Indices into config path arrays for runtime switching
  int current_model_index{ -1 };
  int current_hdr_index{ 0 };

  /// Active mesh: gltf_scene > gltf_model > cube_mesh.
  [[nodiscard]] const vkwave::Mesh* active_mesh() const;

  /// True if the active model has multiple materials.
  [[nodiscard]] bool has_multi_material() const;

  /// Number of materials (at least 1 for single-material fallback).
  [[nodiscard]] uint32_t material_count() const;

  /// Load a new model, replacing the current one. GPU must be drained by caller.
  void load_model(const vkwave::Device& device, const std::string& path);

  /// Load a new IBL environment. GPU must be drained by caller.
  void load_ibl(const vkwave::Device& device, const std::string& path);

  /// Create 1x1 fallback textures for missing material slots.
  void create_fallback_textures(const vkwave::Device& device);
};
