#pragma once

#include <vkwave/core/frame_resources.h>
#include <vkwave/core/semaphore.h>
#include <vkwave/core/timeline_semaphore.h>

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vkwave
{

class Device;
class Swapchain;

/// Callback type for recording pass commands into a command buffer.
///
/// The callback receives the command buffer and the frame_index for
/// indexing into ring-buffered resources.
using RecordFn = std::function<void(vk::CommandBuffer cmd, uint32_t frame_index)>;

/// Callback type for post-record overlay (e.g. ImGui).
///
/// Called after record_commands() but before cmd.end(), within the
/// same command buffer. The overlay can begin its own render pass.
using PostRecordFn = std::function<void(vk::CommandBuffer cmd, uint32_t frame_index)>;

/// Gating mode for controlling when a group is submitted.
enum class GatingMode
{
  always,         ///< Submit every frame (default)
  display_gated,  ///< Submit only when frame will be displayed (FIFO)
  wall_clock      ///< Submit at most N times per second
};

/// Semaphore + value pair for mixed binary/timeline waits.
struct SemaphoreWait
{
  vk::Semaphore semaphore;
  uint64_t value{ 0 }; ///< 0 for binary semaphores, >0 for timeline
};

/// Base class for groups that submit command buffers each frame.
///
/// Owns the frame submission machinery: ring-buffered command pools/buffers,
/// timeline semaphore, binary present semaphores, and gating state.
///
/// Derived classes (ExecutionGroup) add their own pipeline/resource
/// management on top. The virtual record_commands() hook lets each
/// derived class control what goes into the command buffer.
/// An optional post_record_fn runs after record_commands() in the
/// same command buffer (e.g. ImGui overlay).
class SubmissionGroup
{
public:
  SubmissionGroup(const Device& device, const std::string& name, bool debug);
  virtual ~SubmissionGroup();

  SubmissionGroup(const SubmissionGroup&) = delete;
  SubmissionGroup& operator=(const SubmissionGroup&) = delete;

  void set_record_fn(RecordFn fn);
  void set_post_record_fn(PostRecordFn fn);

  /// Create/recreate size-dependent frame resources (command pools, semaphores).
  /// Derived classes should call this first, then create their own resources.
  virtual void create_frame_resources(const Swapchain& swapchain, uint32_t count);

  /// Create frame resources for offscreen groups (no swapchain, just extent).
  void create_frame_resources_offscreen(vk::Extent2D extent, uint32_t count);

  /// Destroy frame resources. Derived classes should destroy their own first,
  /// then call this base version.
  virtual void destroy_frame_resources();

  /// Configure gating mode. For wall_clock, hz is the maximum submission rate.
  void set_gating(GatingMode mode, float hz = 0.0f);

  /// Returns true if this group should be submitted this frame.
  [[nodiscard]] bool should_submit(float elapsed_time, bool is_fifo) const;

  /// Wait on the timeline for this slot (blocks until GPU finishes previous use).
  /// @param will_submit  Whether this group will actually submit this frame.
  void begin_frame(uint32_t slot_index, bool will_submit = true);

  /// Reset command pool, record via record_commands(), submit.
  /// Wait semaphores can be binary (value=0) or timeline (value>0).
  void submit(uint32_t slot_index,
              std::span<const SemaphoreWait> waits,
              vk::Queue queue,
              float elapsed_time = 0.0f);

  /// Drain all slots via single vkWaitSemaphores call.
  void drain();

  /// Control whether submit() signals binary present semaphores.
  /// Offscreen groups should set this to false (nothing consumes them).
  void set_signal_present(bool b) { m_signal_binary_present = b; }

  /// Get the timeline semaphore handle (for inter-group synchronization).
  [[nodiscard]] vk::Semaphore timeline_semaphore() const;

  /// Get the latest signaled timeline value (0 if never submitted).
  [[nodiscard]] uint64_t latest_signal_value() const;

  [[nodiscard]] vk::Extent2D extent() const { return m_extent; }
  [[nodiscard]] const vk::Semaphore* present_semaphore(uint32_t slot) const;

protected:
  /// Hook called by submit() between cmd.begin() and cmd.end().
  /// Base implementation just calls m_record_fn(cmd, slot_index).
  /// Derived classes override to wrap with render pass begin/end, etc.
  virtual void record_commands(vk::CommandBuffer cmd, uint32_t slot_index,
                               FrameResources& frame);

  const Device& m_device;
  std::string m_name;
  bool m_debug{ false };

  std::vector<FrameResources> m_frames;
  vk::Extent2D m_extent{};
  uint32_t m_current_slot{0};

  RecordFn m_record_fn;
  PostRecordFn m_post_record_fn;

private:
  // Timeline semaphore (replaces per-slot fences)
  std::unique_ptr<TimelineSemaphore> m_timeline;
  std::vector<uint64_t> m_slot_timeline_values;
  uint64_t m_next_timeline_value{ 1 };

  // Binary present semaphores (one per slot, for WSI)
  std::vector<std::unique_ptr<Semaphore>> m_present_semaphores;
  bool m_signal_binary_present{ true };

  // Gating
  GatingMode m_gating{ GatingMode::always };
  float m_target_interval{ 0.0f };
  float m_last_run_time{ 0.0f };
  std::vector<bool> m_slot_submitted;
};

} // namespace vkwave
