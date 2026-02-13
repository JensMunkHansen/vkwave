#include <catch2/catch_test_macros.hpp>

#include <vkwave/core/fence.h>
#include <vkwave/core/semaphore.h>

#include <type_traits>

// Fence and Semaphore are RAII wrappers with non-trivial destructors.
// The render graph's compile-time ownership check (std::is_trivially_destructible)
// relies on this property to prevent passes from owning Vulkan resources.

TEST_CASE("vkwave::core::fence_is_raii", "[core]")
{
  STATIC_REQUIRE_FALSE(std::is_trivially_destructible_v<vkwave::Fence>);
}

TEST_CASE("vkwave::core::fence_is_non_copyable", "[core]")
{
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<vkwave::Fence>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<vkwave::Fence>);
}

TEST_CASE("vkwave::core::fence_is_movable", "[core]")
{
  STATIC_REQUIRE(std::is_move_constructible_v<vkwave::Fence>);
}

TEST_CASE("vkwave::core::semaphore_is_raii", "[core]")
{
  STATIC_REQUIRE_FALSE(std::is_trivially_destructible_v<vkwave::Semaphore>);
}

TEST_CASE("vkwave::core::semaphore_is_non_copyable", "[core]")
{
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<vkwave::Semaphore>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<vkwave::Semaphore>);
}

TEST_CASE("vkwave::core::semaphore_is_movable", "[core]")
{
  STATIC_REQUIRE(std::is_move_constructible_v<vkwave::Semaphore>);
}
