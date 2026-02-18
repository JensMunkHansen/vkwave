#pragma once

#include <type_traits>

namespace vkwave
{

/// CRTP base for render passes.
///
/// Enforces that Derived is trivially destructible at compile time.
/// This guarantees passes do not own RAII-wrapped Vulkan resources
/// (Fence, Semaphore, Image, Buffer, etc.), since those have
/// non-trivial destructors.
///
/// Passes hold only raw vk:: handles (trivially destructible uint64
/// wrappers) received from the graph's resource struct.
///
/// The static_assert is in the constructor, not the destructor.
/// A user-provided constructor does NOT affect trivial destructibility,
/// whereas a user-provided destructor would make all derived classes
/// non-trivially-destructible — defeating the entire check.
template <typename Derived>
struct Pass
{
  Pass()
  {
    static_assert(std::is_trivially_destructible_v<Derived>,
      "Passes must not own Vulkan resources -- "
      "store them in the graph's resource struct");
  }
};

} // namespace vkwave
