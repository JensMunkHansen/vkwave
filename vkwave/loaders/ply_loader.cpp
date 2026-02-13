#include <vkwave/loaders/ply_loader.h>
#include <vkwave/loaders/miniply.h>

#include <spdlog/spdlog.h>

#include <filesystem>

namespace vkwave
{

std::unique_ptr<Mesh> load_ply(const Device& device, const std::string& filepath)
{
  // Check file exists
  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("PLY file not found: {}", filepath);
    return nullptr;
  }

  miniply::PLYReader reader(filepath.c_str());
  if (!reader.valid())
  {
    spdlog::error("Failed to open PLY file: {}", filepath);
    return nullptr;
  }

  // Extract filename for mesh name
  std::string mesh_name = std::filesystem::path(filepath).stem().string();

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> colors;

  bool has_normals = false;
  bool has_colors = false;

  // Process elements
  while (reader.has_element())
  {
    if (reader.element_is(miniply::kPLYVertexElement))
    {
      uint32_t num_verts = reader.num_rows();
      if (num_verts == 0)
      {
        spdlog::error("PLY file has no vertices: {}", filepath);
        return nullptr;
      }

      // Find position properties (required)
      uint32_t pos_idxs[3];
      if (!reader.find_pos(pos_idxs))
      {
        spdlog::error("PLY file missing position properties: {}", filepath);
        return nullptr;
      }

      // Find optional properties
      uint32_t normal_idxs[3];
      has_normals = reader.find_normal(normal_idxs);

      uint32_t color_idxs[3];
      has_colors = reader.find_color(color_idxs);

      // Load the element data
      if (!reader.load_element())
      {
        spdlog::error("Failed to load vertex element: {}", filepath);
        return nullptr;
      }

      // Extract positions
      positions.resize(num_verts * 3);
      if (!reader.extract_properties(pos_idxs, 3, miniply::PLYPropertyType::Float, positions.data()))
      {
        spdlog::error("Failed to extract positions: {}", filepath);
        return nullptr;
      }

      // Extract normals if available
      if (has_normals)
      {
        normals.resize(num_verts * 3);
        reader.extract_properties(normal_idxs, 3, miniply::PLYPropertyType::Float, normals.data());
      }

      // Extract colors if available
      if (has_colors)
      {
        // Check if colors are stored as uchar (need normalization) or float
        const miniply::PLYElement* elem = reader.element();
        bool is_uchar_color = (elem->properties[color_idxs[0]].type == miniply::PLYPropertyType::UChar);

        if (is_uchar_color)
        {
          // Extract as uchar then normalize to 0-1
          std::vector<uint8_t> colors_uchar(num_verts * 3);
          reader.extract_properties(color_idxs, 3, miniply::PLYPropertyType::UChar, colors_uchar.data());
          colors.resize(num_verts * 3);
          for (size_t i = 0; i < colors_uchar.size(); i++)
          {
            colors[i] = static_cast<float>(colors_uchar[i]) / 255.0f;
          }
        }
        else
        {
          // Already float, extract directly
          colors.resize(num_verts * 3);
          reader.extract_properties(color_idxs, 3, miniply::PLYPropertyType::Float, colors.data());
        }
      }

      // Build vertex array
      vertices.resize(num_verts);
      for (uint32_t i = 0; i < num_verts; i++)
      {
        vertices[i].position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

        if (has_normals)
        {
          vertices[i].normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        }
        else
        {
          vertices[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (has_colors)
        {
          vertices[i].color = glm::vec3(colors[i * 3 + 0], colors[i * 3 + 1], colors[i * 3 + 2]);
        }
        else
        {
          vertices[i].color = glm::vec3(1.0f);
        }
      }

      spdlog::trace("Loaded {} vertices (normals: {}, colors: {})", num_verts, has_normals, has_colors);
    }
    else if (reader.element_is(miniply::kPLYFaceElement))
    {
      uint32_t num_faces = reader.num_rows();
      if (num_faces == 0)
      {
        reader.next_element();
        continue;
      }

      // Find face indices
      uint32_t indices_idx[1];
      if (!reader.find_indices(indices_idx))
      {
        spdlog::warn("PLY face element missing indices property");
        reader.next_element();
        continue;
      }

      // Load the element data
      if (!reader.load_element())
      {
        spdlog::error("Failed to load face element: {}", filepath);
        return nullptr;
      }

      // Check if triangulation is needed
      uint32_t num_triangles = reader.num_triangles(indices_idx[0]);
      if (num_triangles == 0)
      {
        reader.next_element();
        continue;
      }

      indices.resize(num_triangles * 3);

      if (reader.requires_triangulation(indices_idx[0]))
      {
        // Need to triangulate polygons
        if (!reader.extract_triangles(
              indices_idx[0], positions.data(), static_cast<uint32_t>(vertices.size()),
              miniply::PLYPropertyType::UInt, indices.data()))
        {
          spdlog::error("Failed to triangulate faces: {}", filepath);
          return nullptr;
        }
      }
      else
      {
        // Already triangles, just extract
        if (!reader.extract_list_property(indices_idx[0], miniply::PLYPropertyType::UInt, indices.data()))
        {
          spdlog::error("Failed to extract face indices: {}", filepath);
          return nullptr;
        }
      }

      spdlog::trace("Loaded {} triangles ({} indices)", num_triangles, indices.size());
    }

    reader.next_element();
  }

  if (vertices.empty())
  {
    spdlog::error("No vertices loaded from PLY file: {}", filepath);
    return nullptr;
  }

  // Compute smooth vertex normals if not present or if indices exist
  if (!indices.empty() && (!has_normals || true))  // Always recompute for smooth normals
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

      // Add to each vertex (weighted by area, which is magnitude of cross product)
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

    spdlog::trace("Computed smooth vertex normals");
  }

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

} // namespace vkwave
