#pragma once

#include <vulkan/vulkan.hpp>

namespace vkwave
{

/// @brief Thin wrapper over the RenderDoc in-application API.
///
/// At runtime we ask the OS loader whether the RenderDoc capture library is
/// already mapped into the process (via dlsym(RTLD_DEFAULT, ...) on Linux or
/// GetModuleHandle("renderdoc.dll") on Windows). RenderDoc injects itself when
/// the application is launched through it; if the application is launched
/// standalone the lookup fails and every method becomes a no-op.
///
/// Capturing is scoped: begin_capture() / end_capture() pair captures all
/// command-queue work between them — including out-of-frame compute submits
/// such as IBL cubemap generation that happen before the first vkQueuePresent
/// and are therefore invisible to the normal hotkey-based capture flow.
class RenderDoc
{
public:
  /// @brief Resolve the RenderDoc API. Safe to call multiple times.
  /// @return True when RenderDoc is attached to the process.
  static bool initialize();

  /// @brief Register the Vulkan instance that owns the work to capture.
  ///
  /// RenderDoc identifies a Vulkan device by the dispatch-table pointer
  /// inside the VkInstance (*(void**)instance). Without this binding,
  /// StartFrameCapture(NULL, NULL) produces an empty capture because
  /// RenderDoc cannot associate our queue submits with a captured device.
  static void set_vulkan_instance(vk::Instance instance);

  /// @brief True if the API was resolved successfully.
  static bool is_attached();

  /// @brief Begin a capture scope spanning all queues of the registered device.
  /// No-op when RenderDoc is not attached.
  static void begin_capture();

  /// @brief End the matching capture scope started by begin_capture().
  /// No-op when RenderDoc is not attached.
  static void end_capture();
};

} // namespace vkwave
