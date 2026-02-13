#include <vkwave/core/camera.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace vkwave
{

Camera::Camera()
{
  orthogonalize_view_up();
}

//-----------------------------------------------------------------------------
// Position and Orientation
//-----------------------------------------------------------------------------

void Camera::set_position(float x, float y, float z)
{
  set_position(glm::vec3(x, y, z));
}

void Camera::set_position(const glm::vec3& position)
{
  m_position = position;
  orthogonalize_view_up();
}

void Camera::set_focal_point(float x, float y, float z)
{
  set_focal_point(glm::vec3(x, y, z));
}

void Camera::set_focal_point(const glm::vec3& focal_point)
{
  m_focal_point = focal_point;
  orthogonalize_view_up();
}

void Camera::set_view_up(float x, float y, float z)
{
  set_view_up(glm::vec3(x, y, z));
}

void Camera::set_view_up(const glm::vec3& view_up)
{
  m_view_up = view_up;
  orthogonalize_view_up();
}

float Camera::distance() const
{
  return glm::length(m_position - m_focal_point);
}

glm::vec3 Camera::direction_of_projection() const
{
  glm::vec3 dir = m_focal_point - m_position;
  float len = glm::length(dir);
  if (len < 1e-6f)
  {
    return glm::vec3(0.0f, 0.0f, -1.0f);
  }
  return dir / len;
}

//-----------------------------------------------------------------------------
// View Frustum
//-----------------------------------------------------------------------------

void Camera::set_clipping_range(float near_plane, float far_plane)
{
  m_near_plane = std::max(near_plane, 0.0001f);
  m_far_plane = std::max(far_plane, m_near_plane + 0.0001f);
}

void Camera::set_view_angle(float angle_degrees)
{
  m_view_angle = std::clamp(angle_degrees, 1.0f, 179.0f);
}

void Camera::set_aspect_ratio(float aspect)
{
  m_aspect_ratio = std::max(aspect, 0.001f);
}

void Camera::set_parallel_projection(bool parallel)
{
  m_parallel_projection = parallel;
}

void Camera::set_parallel_scale(float scale)
{
  m_parallel_scale = std::max(scale, 0.0001f);
}

//-----------------------------------------------------------------------------
// Camera Movements
//-----------------------------------------------------------------------------

void Camera::azimuth(float angle_degrees)
{
  // Rotate position around focal point about view-up vector
  float angle_rad = glm::radians(angle_degrees);

  glm::vec3 offset = m_position - m_focal_point;
  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle_rad, m_view_up);
  glm::vec3 new_offset = glm::vec3(rotation * glm::vec4(offset, 0.0f));

  m_position = m_focal_point + new_offset;
  orthogonalize_view_up();
}

void Camera::elevation(float angle_degrees)
{
  // Rotate position around focal point about the right vector
  float angle_rad = glm::radians(angle_degrees);

  glm::vec3 direction = direction_of_projection();
  glm::vec3 right = glm::normalize(glm::cross(direction, m_view_up));

  glm::vec3 offset = m_position - m_focal_point;
  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle_rad, right);
  glm::vec3 new_offset = glm::vec3(rotation * glm::vec4(offset, 0.0f));

  m_position = m_focal_point + new_offset;

  // Also rotate view-up to maintain orientation
  m_view_up = glm::normalize(glm::vec3(rotation * glm::vec4(m_view_up, 0.0f)));
  orthogonalize_view_up();
}

void Camera::roll(float angle_degrees)
{
  // Rotate view-up about the direction of projection
  float angle_rad = glm::radians(angle_degrees);

  glm::vec3 direction = direction_of_projection();
  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle_rad, direction);

  m_view_up = glm::normalize(glm::vec3(rotation * glm::vec4(m_view_up, 0.0f)));
  orthogonalize_view_up();
}

void Camera::yaw(float angle_degrees)
{
  // Rotate focal point around position about view-up vector
  float angle_rad = glm::radians(angle_degrees);

  glm::vec3 offset = m_focal_point - m_position;
  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle_rad, m_view_up);
  glm::vec3 new_offset = glm::vec3(rotation * glm::vec4(offset, 0.0f));

  m_focal_point = m_position + new_offset;
  orthogonalize_view_up();
}

void Camera::pitch(float angle_degrees)
{
  // Rotate focal point around position about the right vector
  float angle_rad = glm::radians(angle_degrees);

  glm::vec3 direction = direction_of_projection();
  glm::vec3 right = glm::normalize(glm::cross(direction, m_view_up));

  glm::vec3 offset = m_focal_point - m_position;
  glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle_rad, right);
  glm::vec3 new_offset = glm::vec3(rotation * glm::vec4(offset, 0.0f));

  m_focal_point = m_position + new_offset;

  // Also rotate view-up to maintain orientation
  m_view_up = glm::normalize(glm::vec3(rotation * glm::vec4(m_view_up, 0.0f)));
  orthogonalize_view_up();
}

void Camera::dolly(float factor)
{
  if (factor <= 0.0f)
  {
    return;
  }

  // Move camera position toward/away from focal point
  glm::vec3 direction = direction_of_projection();
  float dist = distance();
  float new_dist = dist / factor;

  m_position = m_focal_point - direction * new_dist;
}

void Camera::pan(float dx, float dy)
{
  // Compute the view coordinate system
  glm::vec3 direction = direction_of_projection();
  glm::vec3 right = glm::normalize(glm::cross(direction, m_view_up));
  glm::vec3 up = glm::normalize(glm::cross(right, direction));

  // Move both position and focal point
  glm::vec3 offset = right * dx + up * dy;
  m_position += offset;
  m_focal_point += offset;
}

void Camera::zoom(float factor)
{
  if (factor <= 0.0f)
  {
    return;
  }

  if (m_parallel_projection)
  {
    m_parallel_scale /= factor;
    m_parallel_scale = std::max(m_parallel_scale, 0.0001f);
  }
  else
  {
    m_view_angle /= factor;
    m_view_angle = std::clamp(m_view_angle, 1.0f, 179.0f);
  }
}

void Camera::reset_camera(const float bounds[6])
{
  // Compute bounding box center and size
  glm::vec3 center(
    (bounds[0] + bounds[1]) * 0.5f,
    (bounds[2] + bounds[3]) * 0.5f,
    (bounds[4] + bounds[5]) * 0.5f);

  glm::vec3 size(
    bounds[1] - bounds[0],
    bounds[3] - bounds[2],
    bounds[5] - bounds[4]);

  float radius = glm::length(size) * 0.5f;

  // Set focal point to center
  m_focal_point = center;

  // Position camera to see entire bounding sphere
  glm::vec3 direction = direction_of_projection();
  if (glm::length(direction) < 1e-6f)
  {
    direction = glm::vec3(0.0f, 0.0f, -1.0f);
  }

  float distance;
  if (m_parallel_projection)
  {
    m_parallel_scale = radius;
    distance = radius * 3.0f;
  }
  else
  {
    float half_angle = glm::radians(m_view_angle * 0.5f);
    distance = radius / std::sin(half_angle);
  }

  m_position = m_focal_point - direction * distance;

  // Reset clipping range
  reset_clipping_range(bounds);

  spdlog::info("reset_camera: center=({},{},{}), size=({},{},{}), radius={}, distance={}, "
    "pos=({},{},{}), dir=({},{},{}), near={}, far={}, fov={}",
    center.x, center.y, center.z, size.x, size.y, size.z, radius, distance,
    m_position.x, m_position.y, m_position.z,
    direction.x, direction.y, direction.z,
    m_near_plane, m_far_plane, m_view_angle);
}

void Camera::reset_clipping_range(const float bounds[6])
{
  // Use bounding sphere for rotation-invariant clipping range.
  glm::vec3 center(
    (bounds[0] + bounds[1]) * 0.5f,
    (bounds[2] + bounds[3]) * 0.5f,
    (bounds[4] + bounds[5]) * 0.5f);

  glm::vec3 size(
    bounds[1] - bounds[0],
    bounds[3] - bounds[2],
    bounds[5] - bounds[4]);

  float radius = glm::length(size) * 0.5f;
  float dist = glm::length(m_position - center);

  m_near_plane = std::max(0.001f, dist - radius);
  m_far_plane = std::max(m_near_plane + 0.001f, dist + radius);
}

//-----------------------------------------------------------------------------
// Matrix Computation
//-----------------------------------------------------------------------------

glm::mat4 Camera::view_matrix() const
{
  return glm::lookAt(m_position, m_focal_point, m_view_up);
}

glm::mat4 Camera::projection_matrix() const
{
  glm::mat4 proj;

  if (m_parallel_projection)
  {
    float half_width = m_parallel_scale * m_aspect_ratio;
    float half_height = m_parallel_scale;
    proj = glm::ortho(-half_width, half_width, -half_height, half_height,
                      m_near_plane, m_far_plane);
  }
  else
  {
    proj = glm::perspective(glm::radians(m_view_angle), m_aspect_ratio,
                            m_near_plane, m_far_plane);
  }

  if (m_use_vulkan_clip)
  {
    // Vulkan clip space: Y is inverted compared to OpenGL.
    // GLM_FORCE_DEPTH_ZERO_TO_ONE (set in CMakeLists.txt) handles Z [0,1].
    proj[1][1] *= -1.0f;
  }

  return proj;
}

glm::mat4 Camera::view_projection_matrix() const
{
  return projection_matrix() * view_matrix();
}

//-----------------------------------------------------------------------------
// Convenience Methods
//-----------------------------------------------------------------------------

void Camera::set(const glm::vec3& position, const glm::vec3& focal_point,
                 const glm::vec3& view_up)
{
  m_position = position;
  m_focal_point = focal_point;
  m_view_up = view_up;
  orthogonalize_view_up();
}

void Camera::set_use_vulkan_clip(bool use_vulkan)
{
  m_use_vulkan_clip = use_vulkan;
}

//-----------------------------------------------------------------------------
// Private Methods
//-----------------------------------------------------------------------------

void Camera::orthogonalize_view_up()
{
  // Ensure view-up is orthogonal to view direction
  glm::vec3 direction = direction_of_projection();

  // Project view-up onto plane perpendicular to direction
  glm::vec3 right = glm::cross(direction, m_view_up);
  float right_len = glm::length(right);

  if (right_len < 1e-6f)
  {
    // View-up is parallel to direction, pick a different up vector
    if (std::abs(direction.y) < 0.9f)
    {
      m_view_up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else
    {
      m_view_up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    right = glm::cross(direction, m_view_up);
    right_len = glm::length(right);
  }

  right /= right_len;
  m_view_up = glm::normalize(glm::cross(right, direction));
}

} // namespace vkwave
