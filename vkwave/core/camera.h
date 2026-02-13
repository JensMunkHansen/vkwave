#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vkwave
{

/// @brief VTK-style camera for 3D rendering.
///
/// This camera uses an explicit focal point, position, and view-up vector,
/// providing intuitive orbital controls similar to VTK's vtkCamera.
///
/// The camera defines a view coordinate system where:
/// - The view plane normal points from focal point to position
/// - The view-up vector defines the "up" direction
/// - The right vector is computed as cross(direction, viewUp)
class Camera
{
public:
  Camera();
  ~Camera() = default;

  // Non-copyable but movable
  Camera(const Camera&) = default;
  Camera& operator=(const Camera&) = default;
  Camera(Camera&&) = default;
  Camera& operator=(Camera&&) = default;

  //-------------------------------------------------------------------------
  // Position and Orientation
  //-------------------------------------------------------------------------

  /// Set the camera position in world coordinates.
  void set_position(float x, float y, float z);
  void set_position(const glm::vec3& position);
  [[nodiscard]] glm::vec3 position() const { return m_position; }

  /// Set the focal point - the point the camera looks at.
  void set_focal_point(float x, float y, float z);
  void set_focal_point(const glm::vec3& focal_point);
  [[nodiscard]] glm::vec3 focal_point() const { return m_focal_point; }

  /// Set the view-up vector. This vector is projected to be orthogonal
  /// to the view direction.
  void set_view_up(float x, float y, float z);
  void set_view_up(const glm::vec3& view_up);
  [[nodiscard]] glm::vec3 view_up() const { return m_view_up; }

  /// Get the distance from position to focal point.
  [[nodiscard]] float distance() const;

  /// Get the direction of projection (focal point - position, normalized).
  [[nodiscard]] glm::vec3 direction_of_projection() const;

  //-------------------------------------------------------------------------
  // View Frustum
  //-------------------------------------------------------------------------

  /// Set the near and far clipping planes.
  void set_clipping_range(float near_plane, float far_plane);
  [[nodiscard]] float near_plane() const { return m_near_plane; }
  [[nodiscard]] float far_plane() const { return m_far_plane; }

  /// Set the camera view angle (field of view) in degrees.
  /// This is the vertical field of view for perspective projection.
  void set_view_angle(float angle_degrees);
  [[nodiscard]] float view_angle() const { return m_view_angle; }

  /// Set the aspect ratio (width/height).
  void set_aspect_ratio(float aspect);
  [[nodiscard]] float aspect_ratio() const { return m_aspect_ratio; }

  /// Enable/disable parallel (orthographic) projection.
  /// When disabled, perspective projection is used.
  void set_parallel_projection(bool parallel);
  [[nodiscard]] bool parallel_projection() const { return m_parallel_projection; }

  /// Set the parallel scale for orthographic projection.
  /// This is the half-height of the view in world units.
  void set_parallel_scale(float scale);
  [[nodiscard]] float parallel_scale() const { return m_parallel_scale; }

  //-------------------------------------------------------------------------
  // Camera Movements (VTK-style)
  //-------------------------------------------------------------------------

  /// Rotate the camera about the view-up vector centered at the focal point.
  /// Positive angle rotates counter-clockwise when looking down view-up.
  /// @param angle_degrees Rotation angle in degrees.
  void azimuth(float angle_degrees);

  /// Rotate the camera about the cross product of the direction of projection
  /// and the view-up vector, centered at the focal point.
  /// Positive angle rotates the camera up.
  /// @param angle_degrees Rotation angle in degrees.
  void elevation(float angle_degrees);

  /// Rotate the camera about the direction of projection.
  /// Positive angle rotates counter-clockwise when looking along the direction.
  /// @param angle_degrees Rotation angle in degrees.
  void roll(float angle_degrees);

  /// Rotate the focal point about the view-up vector centered at the camera position.
  /// @param angle_degrees Rotation angle in degrees.
  void yaw(float angle_degrees);

  /// Rotate the focal point about the cross product of the direction of projection
  /// and the view-up vector, centered at the camera position.
  /// @param angle_degrees Rotation angle in degrees.
  void pitch(float angle_degrees);

  /// Move the camera position along the direction of projection.
  /// @param factor Scale factor. > 1 moves toward focal point, < 1 moves away.
  void dolly(float factor);

  /// Move both the camera position and focal point along the view plane.
  /// @param dx Horizontal displacement in view coordinates.
  /// @param dy Vertical displacement in view coordinates.
  void pan(float dx, float dy);

  /// Decrease/increase the view angle (zoom in/out) for perspective projection.
  /// For parallel projection, adjusts the parallel scale.
  /// @param factor Scale factor. > 1 zooms in, < 1 zooms out.
  void zoom(float factor);

  /// Reset the camera to view the entire bounding box.
  /// @param bounds Bounding box as [xmin, xmax, ymin, ymax, zmin, zmax].
  void reset_camera(const float bounds[6]);

  /// Reset the camera clipping range based on the visible bounds.
  void reset_clipping_range(const float bounds[6]);

  //-------------------------------------------------------------------------
  // Matrix Computation
  //-------------------------------------------------------------------------

  /// Get the view transformation matrix.
  /// Transforms world coordinates to camera/view coordinates.
  [[nodiscard]] glm::mat4 view_matrix() const;

  /// Get the projection matrix (perspective or orthographic).
  [[nodiscard]] glm::mat4 projection_matrix() const;

  /// Get the combined view-projection matrix.
  [[nodiscard]] glm::mat4 view_projection_matrix() const;

  //-------------------------------------------------------------------------
  // Convenience Methods
  //-------------------------------------------------------------------------

  /// Set all camera parameters at once.
  void set(const glm::vec3& position, const glm::vec3& focal_point, const glm::vec3& view_up);

  /// Apply Vulkan coordinate system correction to projection matrix.
  /// Vulkan has Y-axis pointing down and Z in [0,1] instead of [-1,1].
  void set_use_vulkan_clip(bool use_vulkan);
  [[nodiscard]] bool use_vulkan_clip() const { return m_use_vulkan_clip; }

private:
  void orthogonalize_view_up();

  glm::vec3 m_position{ 0.0f, 0.0f, 1.0f };
  glm::vec3 m_focal_point{ 0.0f, 0.0f, 0.0f };
  glm::vec3 m_view_up{ 0.0f, 1.0f, 0.0f };

  float m_view_angle{ 60.0f };        // Vertical FOV in degrees
  float m_aspect_ratio{ 16.0f / 9.0f };
  float m_near_plane{ 0.1f };
  float m_far_plane{ 1000.0f };

  bool m_parallel_projection{ false };
  float m_parallel_scale{ 1.0f };

  bool m_use_vulkan_clip{ true };
};

} // namespace vkwave
