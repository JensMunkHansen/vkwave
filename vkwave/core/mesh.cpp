#include <vkwave/core/mesh.h>
#include <vkwave/core/device.h>

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
    vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
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
  // Create vertex buffer with ray tracing usage flags
  vk::DeviceSize vertex_buffer_size = sizeof(Vertex) * vertices.size();
  m_vertex_buffer = std::make_unique<Buffer>(device, name + " vertex buffer", vertex_buffer_size,
    vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_vertex_buffer->update(vertices.data(), vertex_buffer_size);

  // Create index buffer with ray tracing usage flags
  vk::DeviceSize index_buffer_size = sizeof(uint32_t) * indices.size();
  m_index_buffer = std::make_unique<Buffer>(device, name + " index buffer", index_buffer_size,
    vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
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

} // namespace vkwave
