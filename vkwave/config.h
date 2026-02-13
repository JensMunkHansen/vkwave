#pragma once

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

// Note: VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 is defined via CMakeLists.txt
// to ensure it's defined before any vulkan.hpp includes in all translation units

#define SHADER_DIR "/home/jmh/github/vkwave/build/linux-clang/vkwave/shaders/"

#define VKWAVE_DEBUG
