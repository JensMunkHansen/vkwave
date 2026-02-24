#pragma once

#include <vkwave/core/semaphore.h>
#include <vkwave/pipeline/execution_group.h>
#include <vkwave/pipeline/submission_group.h>

#include <vulkan/vulkan.hpp>

#include <chrono>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vkwave
{

class Device;
class Swapchain;

/// Concept for pass types usable with RenderGraph::add_pass().
template <typename T>
concept PassInterface = requires(const T ct, T t, vk::CommandBuffer cmd)
{
  { T::pipeline_spec() } -> std::same_as<PipelineSpec>;
  { ct.record(cmd) } -> std::same_as<void>;
  { t.group } -> std::convertible_to<ExecutionGroup*>;
};

/// Top-level frame orchestration.
///
/// Two-tier submission model:
///   - Offscreen groups: submit every frame, no swapchain involvement.
///   - Present group: acquires/presents at display rate, waits on last offscreen output.
///
/// Owns all groups, acquire semaphores, and the CPU frame counter.
class RenderGraph
{
  const Device& m_device;

  // Offscreen groups (submit every CPU frame, no acquire/present)
  std::vector<std::unique_ptr<SubmissionGroup>> m_offscreen_groups;

  // Present group (acquires swapchain image, submits, presents)
  std::unique_ptr<SubmissionGroup> m_present_group;

  std::vector<std::unique_ptr<Semaphore>> m_acquire_semaphores;
  std::vector<uint32_t> m_sem_to_image; // last image_index paired with each sem_index

  uint64_t m_cpu_frame{ 0 };
  uint32_t m_swapchain_image_count{ 0 };
  uint32_t m_offscreen_depth{ 0 }; // ring depth for offscreen groups (0 = use swapchain count)

  // Frame timing (wall-clock)
  using Clock = std::chrono::steady_clock;
  Clock::time_point m_start_time{};
  Clock::time_point m_prev_frame_time{};
  float m_elapsed_time{ 0.0f };
  float m_delta_time{ 0.0f };
  bool m_first_frame{ true };

  uint32_t m_last_offscreen_slot{ 0 };

  // Resize callback — called after offscreen resources are destroyed, before rebuild
  std::function<void(vk::Extent2D)> m_resize_fn;

public:
  explicit RenderGraph(const Device& device);
  ~RenderGraph();

  /// Add an offscreen execution group (no swapchain involvement).
  ExecutionGroup& add_offscreen_group(const std::string& name,
                                       const PipelineSpec& spec,
                                       vk::Format color_format,
                                       bool debug);

  /// Replace an existing offscreen group (destroys old, creates new).
  /// The old group must already have its frame resources destroyed.
  ExecutionGroup& replace_offscreen_group(size_t index,
                                           const std::string& name,
                                           const PipelineSpec& spec,
                                           vk::Format color_format,
                                           bool debug);

  /// Set the present group (renders to swapchain, does acquire/present).
  ExecutionGroup& set_present_group(const std::string& name,
                                     const PipelineSpec& spec,
                                     vk::Format swapchain_format,
                                     bool debug);

  /// Set ring buffer depth for offscreen groups.
  /// Must be called before build(). Default: swapchain image count.
  void set_offscreen_depth(uint32_t n) { m_offscreen_depth = n; }

  /// Set resize callback (for recreating offscreen images).
  void set_resize_fn(std::function<void(vk::Extent2D)> fn) { m_resize_fn = std::move(fn); }

  /// Allocate per-frame resources for all groups.
  void build(const Swapchain& swapchain);

  /// Drain all groups (for shutdown).
  void drain();

  /// Drain + recreate (for resize).
  void resize(const Swapchain& swapchain);

  /// Run a complete frame: submit offscreen groups, conditionally acquire + present.
  /// Returns false on swapchain out-of-date (caller should resize).
  bool render_frame(const Swapchain& swapchain);

  /// Access an offscreen group by index.
  [[nodiscard]] SubmissionGroup& offscreen_group(size_t index)
  {
    return *m_offscreen_groups[index];
  }

  /// Access the present group.
  [[nodiscard]] SubmissionGroup& present_group() { return *m_present_group; }

  [[nodiscard]] uint64_t cpu_frame() const { return m_cpu_frame; }
  [[nodiscard]] uint32_t offscreen_depth() const;

  [[nodiscard]] uint32_t last_offscreen_slot() const { return m_last_offscreen_slot; }

  [[nodiscard]] float elapsed_time() const { return m_elapsed_time; }
  [[nodiscard]] float delta_time() const { return m_delta_time; }
};

} // namespace vkwave
