#include <vkwave/core/mesh.h>
#include <vkwave/core/device.h>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace vkwave
{

Mesh::Mesh(const Device& device, const std::string& name, const std::vector<Vertex>& vertices)
  : m_name(name)
  , m_vertex_count(static_cast<uint32_t>(vertices.size()))
{
  vk::DeviceSize buffer_size = sizeof(Vertex) * vertices.size();

  // Create vertex buffer
  // Using HOST_VISIBLE for simplicity. For better performance, use staging buffer
  // to copy to DEVICE_LOCAL memory.
  // Include ray tracing usage flags for acceleration structure building
  m_vertex_buffer = std::make_unique<Buffer>(device, name + " vertex buffer", buffer_size,
    vk::BufferUsageFlagBits::eVertexBuffer,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

  // Upload vertex data
  m_vertex_buffer->update(vertices.data(), buffer_size);

  spdlog::trace("Created mesh '{}' with {} vertices", name, m_vertex_count);
}

Mesh::Mesh(const Device& device, const std::string& name, const std::vector<Vertex>& vertices,
  const std::vector<uint32_t>& indices)
  : m_name(name)
  , m_vertex_count(static_cast<uint32_t>(vertices.size()))
  , m_index_count(static_cast<uint32_t>(indices.size()))
{
  vk::DeviceSize vertex_buffer_size = sizeof(Vertex) * vertices.size();
  m_vertex_buffer = std::make_unique<Buffer>(device, name + " vertex buffer", vertex_buffer_size,
    vk::BufferUsageFlagBits::eVertexBuffer,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_vertex_buffer->update(vertices.data(), vertex_buffer_size);

  vk::DeviceSize index_buffer_size = sizeof(uint32_t) * indices.size();
  m_index_buffer = std::make_unique<Buffer>(device, name + " index buffer", index_buffer_size,
    vk::BufferUsageFlagBits::eIndexBuffer,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_index_buffer->update(indices.data(), index_buffer_size);

  spdlog::trace(
    "Created mesh '{}' with {} vertices, {} indices", name, m_vertex_count, m_index_count);
}

void Mesh::bind(vk::CommandBuffer cmd) const
{
  vk::Buffer buffers[] = { m_vertex_buffer->buffer() };
  vk::DeviceSize offsets[] = { 0 };
  cmd.bindVertexBuffers(0, 1, buffers, offsets);

  if (m_index_buffer)
  {
    cmd.bindIndexBuffer(m_index_buffer->buffer(), 0, vk::IndexType::eUint32);
  }
}

void Mesh::draw(vk::CommandBuffer cmd) const
{
  if (m_index_buffer)
  {
    cmd.drawIndexed(m_index_count, 1, 0, 0, 0);
  }
  else
  {
    cmd.draw(m_vertex_count, 1, 0, 0);
  }
}

std::unique_ptr<Mesh> Mesh::create_cube(const Device& device)
{
  const float s = 0.5f;

  const glm::vec3 red   { 1.0f, 0.2f, 0.2f };
  const glm::vec3 green { 0.2f, 1.0f, 0.2f };
  const glm::vec3 blue  { 0.2f, 0.4f, 1.0f };
  const glm::vec3 yellow{ 1.0f, 1.0f, 0.2f };
  const glm::vec3 cyan  { 0.2f, 1.0f, 1.0f };
  const glm::vec3 magenta{ 1.0f, 0.2f, 1.0f };

  std::vector<Vertex> vertices = {
    // Front face (+Z)
    { {-s, -s,  s}, { 0,  0,  1}, red },
    { { s, -s,  s}, { 0,  0,  1}, red },
    { { s,  s,  s}, { 0,  0,  1}, red },
    { {-s,  s,  s}, { 0,  0,  1}, red },
    // Back face (-Z)
    { { s, -s, -s}, { 0,  0, -1}, green },
    { {-s, -s, -s}, { 0,  0, -1}, green },
    { {-s,  s, -s}, { 0,  0, -1}, green },
    { { s,  s, -s}, { 0,  0, -1}, green },
    // Right face (+X)
    { { s, -s,  s}, { 1,  0,  0}, blue },
    { { s, -s, -s}, { 1,  0,  0}, blue },
    { { s,  s, -s}, { 1,  0,  0}, blue },
    { { s,  s,  s}, { 1,  0,  0}, blue },
    // Left face (-X)
    { {-s, -s, -s}, {-1,  0,  0}, yellow },
    { {-s, -s,  s}, {-1,  0,  0}, yellow },
    { {-s,  s,  s}, {-1,  0,  0}, yellow },
    { {-s,  s, -s}, {-1,  0,  0}, yellow },
    // Top face (+Y)
    { {-s,  s,  s}, { 0,  1,  0}, cyan },
    { { s,  s,  s}, { 0,  1,  0}, cyan },
    { { s,  s, -s}, { 0,  1,  0}, cyan },
    { {-s,  s, -s}, { 0,  1,  0}, cyan },
    // Bottom face (-Y)
    { {-s, -s, -s}, { 0, -1,  0}, magenta },
    { { s, -s, -s}, { 0, -1,  0}, magenta },
    { { s, -s,  s}, { 0, -1,  0}, magenta },
    { {-s, -s,  s}, { 0, -1,  0}, magenta },
  };

  // Two triangles per face, CCW winding viewed from outside
  std::vector<uint32_t> indices;
  for (uint32_t face = 0; face < 6; ++face)
  {
    uint32_t b = face * 4;
    indices.insert(indices.end(), { b, b+1, b+2, b, b+2, b+3 });
  }

  return std::make_unique<Mesh>(device, "cube", vertices, indices);
}

} // namespace vkwave
