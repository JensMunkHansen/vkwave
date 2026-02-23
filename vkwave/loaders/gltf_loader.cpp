#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <stb_image.h>

#include <vkwave/loaders/gltf_loader.h>
#include <vkwave/core/texture.h>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <unordered_map>

namespace vkwave
{

namespace
{

// Helper to read accessor data as floats
template <typename T>
void read_accessor_data(const cgltf_accessor* accessor, std::vector<T>& out, size_t components)
{
  out.resize(accessor->count * components);
  for (size_t i = 0; i < accessor->count; ++i)
  {
    cgltf_accessor_read_float(accessor, i, reinterpret_cast<float*>(&out[i * components]), components);
  }
}

// Helper to read accessor data as uint32_t indices
void read_index_data(const cgltf_accessor* accessor, std::vector<uint32_t>& out)
{
  out.resize(accessor->count);
  for (size_t i = 0; i < accessor->count; ++i)
  {
    out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, i));
  }
}

} // anonymous namespace

std::unique_ptr<Mesh> load_gltf(const Device& device, const std::string& filepath)
{
  // Check file exists
  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("glTF file not found: {}", filepath);
    return nullptr;
  }

  // Parse glTF file
  cgltf_options options = {};
  cgltf_data* data = nullptr;

  cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to parse glTF file: {} (error {})", filepath, static_cast<int>(result));
    return nullptr;
  }

  // Load buffers (needed for binary data access)
  result = cgltf_load_buffers(&options, data, filepath.c_str());
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to load glTF buffers: {} (error {})", filepath, static_cast<int>(result));
    cgltf_free(data);
    return nullptr;
  }

  // Extract filename for mesh name
  std::string mesh_name = std::filesystem::path(filepath).stem().string();

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  // Process all meshes (combine into one for now)
  for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx)
  {
    const cgltf_mesh& mesh = data->meshes[mesh_idx];

    for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; ++prim_idx)
    {
      const cgltf_primitive& primitive = mesh.primitives[prim_idx];

      // We only handle triangles
      if (primitive.type != cgltf_primitive_type_triangles)
      {
        spdlog::warn("Skipping non-triangle primitive in {}", filepath);
        continue;
      }

      // Find accessors for each attribute
      // glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
      const cgltf_accessor* position_accessor = nullptr;
      const cgltf_accessor* normal_accessor = nullptr;
      const cgltf_accessor* texcoord_accessor = nullptr;
      const cgltf_accessor* color_accessor = nullptr;
      const cgltf_accessor* tangent_accessor = nullptr;

      for (size_t attr_idx = 0; attr_idx < primitive.attributes_count; ++attr_idx)
      {
        const cgltf_attribute& attr = primitive.attributes[attr_idx];

        switch (attr.type)
        {
          case cgltf_attribute_type_position:
            position_accessor = attr.data;
            break;
          case cgltf_attribute_type_normal:
            normal_accessor = attr.data;
            break;
          case cgltf_attribute_type_texcoord:
            if (attr.index == 0) // TEXCOORD_0
              texcoord_accessor = attr.data;
            break;
          case cgltf_attribute_type_color:
            if (attr.index == 0) // COLOR_0
              color_accessor = attr.data;
            break;
          case cgltf_attribute_type_tangent:
            tangent_accessor = attr.data;
            break;
          default:
            break;
        }
      }

      if (!position_accessor)
      {
        spdlog::warn("Primitive missing positions in {}", filepath);
        continue;
      }

      // Read vertex data
      std::vector<float> positions;
      std::vector<float> normals;
      std::vector<float> texcoords;
      std::vector<float> colors;
      std::vector<float> tangents;

      read_accessor_data(position_accessor, positions, 3);

      if (normal_accessor)
      {
        read_accessor_data(normal_accessor, normals, 3);
      }

      if (texcoord_accessor)
      {
        read_accessor_data(texcoord_accessor, texcoords, 2);
      }

      if (color_accessor)
      {
        // Color can be vec3 or vec4
        size_t color_components = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
        read_accessor_data(color_accessor, colors, color_components);
      }

      if (tangent_accessor)
      {
        // glTF TANGENT is always vec4 (xyz=tangent, w=handedness)
        read_accessor_data(tangent_accessor, tangents, 4);
      }

      // Build vertices
      uint32_t base_vertex = static_cast<uint32_t>(vertices.size());
      size_t num_verts = position_accessor->count;

      for (size_t i = 0; i < num_verts; ++i)
      {
        Vertex v;

        v.position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

        if (!normals.empty())
        {
          v.normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        }
        else
        {
          v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (!texcoords.empty())
        {
          v.texCoord = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1]);
        }
        else
        {
          v.texCoord = glm::vec2(0.0f);
        }

        if (!colors.empty())
        {
          size_t color_components = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
          v.color = glm::vec3(colors[i * color_components + 0],
                              colors[i * color_components + 1],
                              colors[i * color_components + 2]);
        }
        else
        {
          v.color = glm::vec3(1.0f);
        }

        if (!tangents.empty())
        {
          // glTF TANGENT: vec4 where xyz=tangent direction, w=handedness (+1 or -1)
          v.tangent = glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                tangents[i * 4 + 2], tangents[i * 4 + 3]);
        }
        else
        {
          // Default tangent along X axis with positive handedness
          v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        vertices.push_back(v);
      }

      // Read indices
      if (primitive.indices)
      {
        std::vector<uint32_t> prim_indices;
        read_index_data(primitive.indices, prim_indices);

        // Offset indices by base vertex
        for (uint32_t idx : prim_indices)
        {
          indices.push_back(base_vertex + idx);
        }
      }
      else
      {
        // Generate sequential indices
        for (size_t i = 0; i < num_verts; ++i)
        {
          indices.push_back(base_vertex + static_cast<uint32_t>(i));
        }
      }
    }
  }

  cgltf_free(data);

  if (vertices.empty())
  {
    spdlog::error("No vertices loaded from glTF file: {}", filepath);
    return nullptr;
  }

  // Compute smooth vertex normals if not present
  bool has_valid_normals = false;
  for (const auto& v : vertices)
  {
    if (glm::length(v.normal) > 0.01f)
    {
      has_valid_normals = true;
      break;
    }
  }

  if (!has_valid_normals && !indices.empty())
  {
    // Reset normals to zero
    for (auto& v : vertices)
    {
      v.normal = glm::vec3(0.0f);
    }

    // Accumulate face normals at each vertex
    for (size_t i = 0; i < indices.size(); i += 3)
    {
      uint32_t i0 = indices[i + 0];
      uint32_t i1 = indices[i + 1];
      uint32_t i2 = indices[i + 2];

      glm::vec3 v0 = vertices[i0].position;
      glm::vec3 v1 = vertices[i1].position;
      glm::vec3 v2 = vertices[i2].position;

      // Compute face normal (cross product of edges)
      glm::vec3 edge1 = v1 - v0;
      glm::vec3 edge2 = v2 - v0;
      glm::vec3 faceNormal = glm::cross(edge1, edge2);

      // Add to each vertex (weighted by area)
      vertices[i0].normal += faceNormal;
      vertices[i1].normal += faceNormal;
      vertices[i2].normal += faceNormal;
    }

    // Normalize all vertex normals
    for (auto& v : vertices)
    {
      if (glm::length(v.normal) > 0.0001f)
      {
        v.normal = glm::normalize(v.normal);
      }
      else
      {
        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
      }
    }

    spdlog::trace("Computed smooth vertex normals for glTF mesh");
  }

  spdlog::info("Loaded glTF mesh '{}': {} vertices, {} indices ({} triangles)",
    mesh_name, vertices.size(), indices.size(), indices.size() / 3);

  // Create mesh
  if (indices.empty())
  {
    return std::make_unique<Mesh>(device, mesh_name, vertices);
  }
  else
  {
    return std::make_unique<Mesh>(device, mesh_name, vertices, indices);
  }
}

namespace
{

/// @brief Extract base color texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
std::unique_ptr<Texture> extract_base_color_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a base color texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];

    if (!material.has_pbr_metallic_roughness)
    {
      continue;
    }

    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const cgltf_texture_view& tex_view = pbr.base_color_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_texture";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded base color texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded base color texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract normal texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-normaltextureinfo
std::unique_ptr<Texture> extract_normal_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a normal texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.normal_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded normal texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_normal";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded normal texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Normal texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string(), true);
        spdlog::info("Loaded normal texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load normal texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract metallic/roughness texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness
/// Note: glTF stores roughness in G channel, metallic in B channel
std::unique_ptr<Texture> extract_metallic_roughness_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a metallic/roughness texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];

    if (!material.has_pbr_metallic_roughness)
    {
      continue;
    }

    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const cgltf_texture_view& tex_view = pbr.metallic_roughness_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded metallic/roughness texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_metallic_roughness";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded metallic/roughness texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Metallic/roughness texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string(), true);
        spdlog::info("Loaded metallic/roughness texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load metallic/roughness texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract emissive texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material
std::unique_ptr<Texture> extract_emissive_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with an emissive texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.emissive_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded emissive texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_emissive";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded emissive texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Emissive texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded emissive texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load emissive texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract ambient occlusion texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-occlusiontextureinfo
/// Note: glTF stores AO in the R channel
std::unique_ptr<Texture> extract_ao_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with an occlusion texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.occlusion_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded AO texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_ao";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded AO texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("AO texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string(), true);
        spdlog::info("Loaded AO texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load AO texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

} // anonymous namespace

GltfModel load_gltf_model(const Device& device, const std::string& filepath)
{
  GltfModel model;

  // Check file exists
  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("glTF file not found: {}", filepath);
    return model;
  }

  std::filesystem::path file_path(filepath);
  std::filesystem::path base_path = file_path.parent_path();

  // Parse glTF file
  cgltf_options options = {};
  cgltf_data* data = nullptr;

  cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to parse glTF file: {} (error {})", filepath, static_cast<int>(result));
    return model;
  }

  // Load buffers (needed for binary data access)
  result = cgltf_load_buffers(&options, data, filepath.c_str());
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to load glTF buffers: {} (error {})", filepath, static_cast<int>(result));
    cgltf_free(data);
    return model;
  }

  // Extract textures first (before we free cgltf_data)
  model.baseColorTexture = extract_base_color_texture(data, device, base_path);
  model.normalTexture = extract_normal_texture(data, device, base_path);
  model.metallicRoughnessTexture = extract_metallic_roughness_texture(data, device, base_path);
  model.emissiveTexture = extract_emissive_texture(data, device, base_path);
  model.aoTexture = extract_ao_texture(data, device, base_path);

  cgltf_free(data);

  // Load mesh using existing function
  model.mesh = load_gltf(device, filepath);

  return model;
}

namespace
{

/// @brief Generic texture extraction from a cgltf_texture_view.
/// Works for any texture slot (base color, normal, metallic/roughness, emissive, AO).
/// @param linear If true, create texture with UNORM format (for data textures like normal/MR/AO).
std::unique_ptr<Texture> extract_texture(
  const cgltf_texture_view& tex_view, const Device& device,
  const std::filesystem::path& base_path, const std::string& slot_name,
  bool linear = false)
{
  if (!tex_view.texture || !tex_view.texture->image)
  {
    return nullptr;
  }

  const cgltf_image* image = tex_view.texture->image;

  if (image->buffer_view)
  {
    const cgltf_buffer_view* buffer_view = image->buffer_view;
    const uint8_t* buffer_data =
      static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
    size_t buffer_size = buffer_view->size;

    int width, height, channels;
    stbi_uc* pixels =
      stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
        &channels, STBI_rgb_alpha);

    if (!pixels)
    {
      spdlog::warn("Failed to decode embedded {} texture", slot_name);
      return nullptr;
    }

    std::string tex_name = image->name ? image->name : ("embedded_" + slot_name);
    auto tex = std::make_unique<Texture>(device, tex_name, pixels,
      static_cast<uint32_t>(width), static_cast<uint32_t>(height), linear);

    stbi_image_free(pixels);

    spdlog::info("Loaded embedded {} texture: {} ({}x{})", slot_name, tex_name, width, height);
    return tex;
  }
  else if (image->uri)
  {
    std::string uri = image->uri;

    if (uri.rfind("data:", 0) == 0)
    {
      spdlog::warn("Data URI textures not supported yet");
      return nullptr;
    }

    std::filesystem::path tex_path = base_path / uri;

    if (!std::filesystem::exists(tex_path))
    {
      spdlog::warn("{} texture file not found: {}", slot_name, tex_path.string());
      return nullptr;
    }

    std::string tex_name = image->name ? image->name : tex_path.stem().string();

    try
    {
      auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string(), linear);
      spdlog::info("Loaded {} texture: {} from {}", slot_name, tex_name, tex_path.string());
      return tex;
    }
    catch (const std::exception& e)
    {
      spdlog::warn("Failed to load {} texture {}: {}", slot_name, tex_path.string(), e.what());
      return nullptr;
    }
  }

  return nullptr;
}

/// @brief Recursively traverse glTF node tree, extracting primitives with world transforms.
void traverse_nodes(
  const cgltf_node* node,
  const cgltf_data* data,
  const Device& device,
  const std::filesystem::path& base_path,
  std::vector<Vertex>& all_vertices,
  std::vector<uint32_t>& all_indices,
  std::vector<ScenePrimitive>& primitives,
  std::vector<SceneMaterial>& materials,
  std::unordered_map<const cgltf_material*, uint32_t>& material_map,
  AABB& bounds)
{
  // Compute world transform for this node
  float m[16];
  cgltf_node_transform_world(node, m);
  // cgltf outputs column-major, same as glm
  glm::mat4 model_matrix(
    m[0], m[1], m[2], m[3],
    m[4], m[5], m[6], m[7],
    m[8], m[9], m[10], m[11],
    m[12], m[13], m[14], m[15]);

  if (node->mesh)
  {
    const cgltf_mesh& mesh = *node->mesh;

    for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; ++prim_idx)
    {
      const cgltf_primitive& primitive = mesh.primitives[prim_idx];

      if (primitive.type != cgltf_primitive_type_triangles)
      {
        spdlog::warn("Skipping non-triangle primitive");
        continue;
      }

      // Resolve material index
      uint32_t mat_index = 0;
      if (primitive.material)
      {
        auto it = material_map.find(primitive.material);
        if (it == material_map.end())
        {
          // New material - extract textures
          mat_index = static_cast<uint32_t>(materials.size());
          material_map[primitive.material] = mat_index;

          SceneMaterial scene_mat;
          if (primitive.material->has_pbr_metallic_roughness)
          {
            scene_mat.baseColorTexture = extract_texture(
              primitive.material->pbr_metallic_roughness.base_color_texture,
              device, base_path, "baseColor");
            scene_mat.metallicRoughnessTexture = extract_texture(
              primitive.material->pbr_metallic_roughness.metallic_roughness_texture,
              device, base_path, "metallicRoughness", true);
          }
          scene_mat.normalTexture = extract_texture(
            primitive.material->normal_texture, device, base_path, "normal", true);
          scene_mat.emissiveTexture = extract_texture(
            primitive.material->emissive_texture, device, base_path, "emissive");
          scene_mat.aoTexture = extract_texture(
            primitive.material->occlusion_texture, device, base_path, "ao", true);

          // Extract material scalar properties
          if (primitive.material->has_pbr_metallic_roughness)
          {
            const auto& pbr = primitive.material->pbr_metallic_roughness;
            const auto& bcf = pbr.base_color_factor;
            scene_mat.baseColorFactor = glm::vec4(bcf[0], bcf[1], bcf[2], bcf[3]);
            scene_mat.metallicFactor = pbr.metallic_factor;
            scene_mat.roughnessFactor = pbr.roughness_factor;
          }
          switch (primitive.material->alpha_mode)
          {
            case cgltf_alpha_mode_mask:
              scene_mat.alphaMode = AlphaMode::Mask;
              break;
            case cgltf_alpha_mode_blend:
              scene_mat.alphaMode = AlphaMode::Blend;
              break;
            default:
              scene_mat.alphaMode = AlphaMode::Opaque;
              break;
          }
          scene_mat.alphaCutoff = primitive.material->alpha_cutoff;
          scene_mat.doubleSided = primitive.material->double_sided;

          // KHR_materials_iridescence
          if (primitive.material->has_iridescence)
          {
            const auto& irid = primitive.material->iridescence;
            scene_mat.iridescenceTexture = extract_texture(
              irid.iridescence_texture, device, base_path, "iridescence", true);
            scene_mat.iridescenceThicknessTexture = extract_texture(
              irid.iridescence_thickness_texture, device, base_path, "iridescenceThickness", true);
            scene_mat.iridescenceFactor = irid.iridescence_factor;
            scene_mat.iridescenceIor = irid.iridescence_ior;
            scene_mat.iridescenceThicknessMin = irid.iridescence_thickness_min;
            scene_mat.iridescenceThicknessMax = irid.iridescence_thickness_max;
          }

          // KHR_materials_transmission
          if (primitive.material->has_transmission)
          {
            scene_mat.transmissionFactor =
              primitive.material->transmission.transmission_factor;
          }

          // KHR_materials_diffuse_transmission — not natively supported by cgltf,
          // so we parse the raw extension JSON from material->extensions[].
          // Uses diffuseTransmissionFactor as our transmissionFactor.
          if (scene_mat.transmissionFactor == 0.0f)
          {
            for (cgltf_size ei = 0; ei < primitive.material->extensions_count; ++ei)
            {
              const auto& ext = primitive.material->extensions[ei];
              if (ext.name
                  && std::string_view(ext.name) == "KHR_materials_diffuse_transmission"
                  && ext.data)
              {
                // Minimal JSON parse: find "diffuseTransmissionFactor" : <number>
                std::string_view json(ext.data);
                auto pos = json.find("\"diffuseTransmissionFactor\"");
                if (pos != std::string_view::npos)
                {
                  pos = json.find(':', pos);
                  if (pos != std::string_view::npos)
                  {
                    float val = 0.0f;
                    auto start = json.find_first_of("0123456789.-", pos + 1);
                    if (start != std::string_view::npos)
                    {
                      val = std::stof(std::string(json.substr(start)));
                    }
                    scene_mat.transmissionFactor = val;
                    spdlog::info("  KHR_materials_diffuse_transmission: factor={:.3f}",
                      val);
                  }
                }
                else
                {
                  // Extension present but no factor → default 0.0 per spec
                  // (non-zero means some diffuse transmission)
                  scene_mat.transmissionFactor = 1.0f;
                }
                break;
              }
            }
          }

          // Fallback: volume with thickness but no transmission of any kind →
          // treat as transmissive, but flag the shader to derive per-pixel
          // transmission from the thickness texture instead of using a scalar.
          if (primitive.material->has_volume && scene_mat.transmissionFactor == 0.0f
              && primitive.material->volume.thickness_factor > 0.0f)
          {
            scene_mat.transmissionFactor = 1.0f;
            scene_mat.deriveTransmissionFromThickness = true;
          }

          // KHR_materials_volume
          if (primitive.material->has_volume)
          {
            const auto& vol = primitive.material->volume;
            scene_mat.thicknessFactor = vol.thickness_factor;
            scene_mat.thicknessTexture = extract_texture(
              vol.thickness_texture, device, base_path, "thickness", true);
            scene_mat.attenuationColor = glm::vec3(
              vol.attenuation_color[0], vol.attenuation_color[1], vol.attenuation_color[2]);
            scene_mat.attenuationDistance = vol.attenuation_distance;
          }

          materials.push_back(std::move(scene_mat));
        }
        else
        {
          mat_index = it->second;
        }
      }
      else
      {
        // No material assigned - ensure we have a default (index 0)
        if (material_map.find(nullptr) == material_map.end())
        {
          material_map[nullptr] = static_cast<uint32_t>(materials.size());
          materials.emplace_back(); // empty SceneMaterial (all nullptr textures)
        }
        mat_index = material_map[nullptr];
      }

      // Find accessors
      const cgltf_accessor* position_accessor = nullptr;
      const cgltf_accessor* normal_accessor = nullptr;
      const cgltf_accessor* texcoord_accessor = nullptr;
      const cgltf_accessor* color_accessor = nullptr;
      const cgltf_accessor* tangent_accessor = nullptr;

      for (size_t attr_idx = 0; attr_idx < primitive.attributes_count; ++attr_idx)
      {
        const cgltf_attribute& attr = primitive.attributes[attr_idx];
        switch (attr.type)
        {
          case cgltf_attribute_type_position:
            position_accessor = attr.data;
            break;
          case cgltf_attribute_type_normal:
            normal_accessor = attr.data;
            break;
          case cgltf_attribute_type_texcoord:
            if (attr.index == 0) texcoord_accessor = attr.data;
            break;
          case cgltf_attribute_type_color:
            if (attr.index == 0) color_accessor = attr.data;
            break;
          case cgltf_attribute_type_tangent:
            tangent_accessor = attr.data;
            break;
          default:
            break;
        }
      }

      if (!position_accessor)
      {
        spdlog::warn("Primitive missing positions, skipping");
        continue;
      }

      // Read vertex data
      std::vector<float> positions, normals, texcoords, colors, tangents;
      read_accessor_data(position_accessor, positions, 3);
      if (normal_accessor) read_accessor_data(normal_accessor, normals, 3);
      if (texcoord_accessor) read_accessor_data(texcoord_accessor, texcoords, 2);
      if (color_accessor)
      {
        size_t cc = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
        read_accessor_data(color_accessor, colors, cc);
      }
      if (tangent_accessor) read_accessor_data(tangent_accessor, tangents, 4);

      // Record vertex offset for this primitive
      int32_t vertex_offset = static_cast<int32_t>(all_vertices.size());
      size_t num_verts = position_accessor->count;

      for (size_t i = 0; i < num_verts; ++i)
      {
        Vertex v;
        v.position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

        // Expand world-space bounding box
        glm::vec3 world_pos = glm::vec3(model_matrix * glm::vec4(v.position, 1.0f));
        bounds.expand(world_pos);

        if (!normals.empty())
          v.normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        else
          v.normal = glm::vec3(0.0f, 0.0f, 1.0f);

        if (!texcoords.empty())
          v.texCoord = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1]);
        else
          v.texCoord = glm::vec2(0.0f);

        if (!colors.empty())
        {
          size_t cc = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
          v.color = glm::vec3(colors[i * cc + 0], colors[i * cc + 1], colors[i * cc + 2]);
        }
        else
          v.color = glm::vec3(1.0f);

        if (!tangents.empty())
          v.tangent = glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                tangents[i * 4 + 2], tangents[i * 4 + 3]);
        else
          v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

        all_vertices.push_back(v);
      }

      // Read indices (raw, not offset - we use vertexOffset in drawIndexed)
      uint32_t first_index = static_cast<uint32_t>(all_indices.size());
      uint32_t index_count = 0;

      if (primitive.indices)
      {
        std::vector<uint32_t> prim_indices;
        read_index_data(primitive.indices, prim_indices);
        index_count = static_cast<uint32_t>(prim_indices.size());
        all_indices.insert(all_indices.end(), prim_indices.begin(), prim_indices.end());
      }
      else
      {
        index_count = static_cast<uint32_t>(num_verts);
        for (size_t i = 0; i < num_verts; ++i)
        {
          all_indices.push_back(static_cast<uint32_t>(i));
        }
      }

      // Compute centroid (average of all vertex positions in object space)
      glm::vec3 centroid(0.0f);
      for (size_t i = 0; i < num_verts; ++i)
      {
        centroid += glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
      }
      if (num_verts > 0)
      {
        centroid /= static_cast<float>(num_verts);
      }

      ScenePrimitive scene_prim;
      scene_prim.firstIndex = first_index;
      scene_prim.indexCount = index_count;
      scene_prim.vertexOffset = vertex_offset;
      scene_prim.materialIndex = mat_index;
      scene_prim.modelMatrix = model_matrix;
      scene_prim.centroid = centroid;
      primitives.push_back(scene_prim);
    }
  }

  // Recurse into children
  for (size_t i = 0; i < node->children_count; ++i)
  {
    traverse_nodes(node->children[i], data, device, base_path,
      all_vertices, all_indices, primitives, materials, material_map, bounds);
  }
}

} // anonymous namespace

GltfScene load_gltf_scene(const Device& device, const std::string& filepath)
{
  GltfScene scene;

  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("glTF file not found: {}", filepath);
    return scene;
  }

  std::filesystem::path file_path(filepath);
  std::filesystem::path base_path = file_path.parent_path();

  cgltf_options options = {};
  cgltf_data* data = nullptr;

  cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to parse glTF file: {} (error {})", filepath, static_cast<int>(result));
    return scene;
  }

  result = cgltf_load_buffers(&options, data, filepath.c_str());
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to load glTF buffers: {} (error {})", filepath, static_cast<int>(result));
    cgltf_free(data);
    return scene;
  }

  std::vector<Vertex> all_vertices;
  std::vector<uint32_t> all_indices;
  std::unordered_map<const cgltf_material*, uint32_t> material_map;

  // Traverse all scene nodes
  for (size_t s = 0; s < data->scenes_count; ++s)
  {
    const cgltf_scene& gltf_scene = data->scenes[s];
    for (size_t n = 0; n < gltf_scene.nodes_count; ++n)
    {
      traverse_nodes(gltf_scene.nodes[n], data, device, base_path,
        all_vertices, all_indices, scene.primitives, scene.materials, material_map,
        scene.bounds);
    }
  }

  cgltf_free(data);

  if (all_vertices.empty())
  {
    spdlog::error("No vertices loaded from glTF scene: {}", filepath);
    return scene;
  }

  std::string mesh_name = file_path.stem().string();

  if (all_indices.empty())
  {
    scene.mesh = std::make_unique<Mesh>(device, mesh_name, all_vertices);
  }
  else
  {
    scene.mesh = std::make_unique<Mesh>(device, mesh_name, all_vertices, all_indices);
  }

  spdlog::info("Loaded glTF scene '{}': {} vertices, {} indices ({} triangles), {} primitives, {} materials",
    mesh_name, all_vertices.size(), all_indices.size(), all_indices.size() / 3,
    scene.primitives.size(), scene.materials.size());

  return scene;
}

} // namespace vkwave
