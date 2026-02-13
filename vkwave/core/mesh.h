#pragma once

#include <vkwave/core/buffer.h>
#include <vkwave/core/vertex.h>

#include <memory>
#include <vector>

namespace vkwave
{

class Device;

/// @brief Mesh class for rendering geometry.
///
/// Holds vertex and optional index buffers for GPU rendering.
/// Supports both indexed and non-indexed drawing.
class Mesh
{
public:
  /// @brief Create a mesh from vertex data (non-indexed).
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the mesh.
  /// @param vertices Vertex data.
  Mesh(const Device& device, const std::string& name, const std::vector<Vertex>& vertices);

  /// @brief Create a mesh from vertex and index data (indexed).
  /// @param device The Vulkan device wrapper.
  /// @param name Debug name for the mesh.
  /// @param vertices Vertex data.
  /// @param indices Index data.
  Mesh(const Device& device, const std::string& name, const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices);

  ~Mesh() = default;

  // Non-copyable
  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;

  // Movable
  Mesh(Mesh&&) = default;
  Mesh& operator=(Mesh&&) = default;

  /// @brief Bind vertex (and index) buffers to command buffer.
  /// @param cmd The command buffer to bind to.
  void bind(vk::CommandBuffer cmd) const;

  /// @brief Record draw command.
  /// @param cmd The command buffer to record to.
  void draw(vk::CommandBuffer cmd) const;

  /// @brief Get the number of vertices.
  [[nodiscard]] uint32_t vertex_count() const { return m_vertex_count; }

  /// @brief Get the number of indices (0 if non-indexed).
  [[nodiscard]] uint32_t index_count() const { return m_index_count; }

  /// @brief Check if mesh uses indexed drawing.
  [[nodiscard]] bool is_indexed() const { return m_index_count > 0; }

  /// @brief Get the mesh name.
  [[nodiscard]] const std::string& name() const { return m_name; }

  /// @brief Get the vertex buffer handle (for ray tracing).
  [[nodiscard]] vk::Buffer vertex_buffer() const { return m_vertex_buffer->buffer(); }

  /// @brief Get the index buffer handle (for ray tracing).
  [[nodiscard]] vk::Buffer index_buffer() const { return m_index_buffer ? m_index_buffer->buffer() : VK_NULL_HANDLE; }

private:
  std::string m_name;

  std::unique_ptr<Buffer> m_vertex_buffer;
  std::unique_ptr<Buffer> m_index_buffer;

  uint32_t m_vertex_count{ 0 };
  uint32_t m_index_count{ 0 };
};

} // namespace vkwave
