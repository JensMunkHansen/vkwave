#pragma once

#include <glm/glm.hpp>

namespace vkwave
{

/// Base class for all light sources
class Light
{
public:
  virtual ~Light() = default;

  /// Get light position/direction for shader (xyz = pos/dir, w = type indicator)
  [[nodiscard]] virtual glm::vec4 position_or_direction() const = 0;

  // Common properties
  void set_color(const glm::vec3& color) { m_color = color; }
  void set_color(float r, float g, float b) { m_color = glm::vec3(r, g, b); }
  [[nodiscard]] glm::vec3 color() const { return m_color; }

  void set_intensity(float intensity) { m_intensity = intensity; }
  [[nodiscard]] float intensity() const { return m_intensity; }

  void set_ambient(const glm::vec3& ambient) { m_ambient = ambient; }
  void set_ambient(float r, float g, float b) { m_ambient = glm::vec3(r, g, b); }
  [[nodiscard]] glm::vec3 ambient() const { return m_ambient; }

  /// Get color with intensity applied (for shader)
  [[nodiscard]] glm::vec4 color_with_intensity() const
  {
    return glm::vec4(m_color, m_intensity);
  }

  /// Get ambient as vec4 (for shader)
  [[nodiscard]] glm::vec4 ambient_vec4() const
  {
    return glm::vec4(m_ambient, 1.0f);
  }

protected:
  glm::vec3 m_color{ 1.0f, 1.0f, 1.0f };
  float m_intensity{ 1.0f };
  glm::vec3 m_ambient{ 0.15f, 0.15f, 0.15f };
};

/// Directional light (e.g., sun) - parallel rays from infinite distance
class DirectionalLight : public Light
{
public:
  DirectionalLight() = default;
  DirectionalLight(const glm::vec3& direction)
    : m_direction(glm::normalize(direction))
  {
  }

  void set_direction(const glm::vec3& direction) { m_direction = glm::normalize(direction); }
  void set_direction(float x, float y, float z) { m_direction = glm::normalize(glm::vec3(x, y, z)); }
  [[nodiscard]] glm::vec3 direction() const { return m_direction; }

  /// Returns direction with w=0 (indicates directional light in shader)
  [[nodiscard]] glm::vec4 position_or_direction() const override
  {
    return glm::vec4(m_direction, 0.0f);
  }

private:
  glm::vec3 m_direction{ 0.0f, 0.0f, 1.0f };
};

/// Point light - emits light in all directions from a position
class PointLight : public Light
{
public:
  PointLight() = default;
  PointLight(const glm::vec3& position)
    : m_position(position)
  {
  }

  void set_position(const glm::vec3& position) { m_position = position; }
  void set_position(float x, float y, float z) { m_position = glm::vec3(x, y, z); }
  [[nodiscard]] glm::vec3 position() const { return m_position; }

  // Attenuation factors: 1 / (constant + linear*d + quadratic*d^2)
  void set_attenuation(float constant, float linear, float quadratic)
  {
    m_attenuation_constant = constant;
    m_attenuation_linear = linear;
    m_attenuation_quadratic = quadratic;
  }

  [[nodiscard]] float attenuation_constant() const { return m_attenuation_constant; }
  [[nodiscard]] float attenuation_linear() const { return m_attenuation_linear; }
  [[nodiscard]] float attenuation_quadratic() const { return m_attenuation_quadratic; }

  /// Returns position with w=1 (indicates point light in shader)
  [[nodiscard]] glm::vec4 position_or_direction() const override
  {
    return glm::vec4(m_position, 1.0f);
  }

private:
  glm::vec3 m_position{ 0.0f, 0.0f, 0.0f };
  float m_attenuation_constant{ 1.0f };
  float m_attenuation_linear{ 0.09f };
  float m_attenuation_quadratic{ 0.032f };
};

/// Spot light - emits light in a cone from a position
class SpotLight : public Light
{
public:
  SpotLight() = default;
  SpotLight(const glm::vec3& position, const glm::vec3& direction)
    : m_position(position)
    , m_direction(glm::normalize(direction))
  {
  }

  void set_position(const glm::vec3& position) { m_position = position; }
  void set_position(float x, float y, float z) { m_position = glm::vec3(x, y, z); }
  [[nodiscard]] glm::vec3 position() const { return m_position; }

  void set_direction(const glm::vec3& direction) { m_direction = glm::normalize(direction); }
  void set_direction(float x, float y, float z) { m_direction = glm::normalize(glm::vec3(x, y, z)); }
  [[nodiscard]] glm::vec3 direction() const { return m_direction; }

  /// Set cone angles in degrees
  void set_cutoff_angles(float inner_degrees, float outer_degrees)
  {
    m_inner_cutoff = glm::cos(glm::radians(inner_degrees));
    m_outer_cutoff = glm::cos(glm::radians(outer_degrees));
  }

  [[nodiscard]] float inner_cutoff() const { return m_inner_cutoff; }
  [[nodiscard]] float outer_cutoff() const { return m_outer_cutoff; }

  /// Returns position with w=2 (indicates spot light in shader)
  [[nodiscard]] glm::vec4 position_or_direction() const override
  {
    return glm::vec4(m_position, 2.0f);
  }

  /// Get direction as vec4 for shader
  [[nodiscard]] glm::vec4 direction_vec4() const
  {
    return glm::vec4(m_direction, 0.0f);
  }

private:
  glm::vec3 m_position{ 0.0f, 0.0f, 0.0f };
  glm::vec3 m_direction{ 0.0f, 0.0f, -1.0f };
  float m_inner_cutoff{ glm::cos(glm::radians(12.5f)) };
  float m_outer_cutoff{ glm::cos(glm::radians(17.5f)) };
};

} // namespace vkwave
