#pragma once

#include <vkwave/core/mesh.h>

#include <memory>
#include <string>

namespace vkwave
{

class Device;

/// @brief Load a PLY mesh file.
///
/// Supports ASCII and binary PLY files with vertex positions, normals, and colors.
/// Uses miniply for parsing.
///
/// @param device The Vulkan device wrapper.
/// @param filepath Path to the PLY file.
/// @return Loaded mesh, or nullptr on failure.
std::unique_ptr<Mesh> load_ply(const Device& device, const std::string& filepath);

} // namespace vkwave
