set(TARGET_NAME vkwave)

# --- Resolve dependency targets ---

# spdlog
if(TARGET spdlog::spdlog)
  set(SPDLOG_TARGET spdlog::spdlog)
elseif(TARGET spdlog)
  set(SPDLOG_TARGET spdlog)
endif()
message(STATUS "spdlog: using ${SPDLOG_TARGET}")

# glfw
if(TARGET glfw)
  set(GLFW_TARGET glfw)
elseif(TARGET glfw3::glfw3)
  set(GLFW_TARGET glfw3::glfw3)
endif()
message(STATUS "glfw: using ${GLFW_TARGET}")

# imgui (pre-built with GLFW/Vulkan backends)
add_library(imgui_impl INTERFACE)
target_link_libraries(imgui_impl INTERFACE imgui::imgui)

# --- Generate config.h from template ---
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/config.h"
  @ONLY
)

# --- Library sources ---
add_library(${TARGET_NAME}
  # core
  core/window.cpp
  core/instance.cpp
  core/device.cpp
  core/fence.cpp
  core/semaphore.cpp
  core/exception.cpp
  core/swapchain.cpp
  core/windowsurface.cpp
  core/commands.cpp
  core/buffer.cpp
  core/image.cpp
  core/mesh.cpp
  core/texture.cpp
  core/depth_stencil_attachment.cpp
  core/camera.cpp
  core/enumerate.cpp
  core/representation.cpp
  core/frame_resources.cpp
  core/timeline_semaphore.cpp
  # pipeline
  pipeline/shaders.cpp
  pipeline/shader_compiler.cpp
  pipeline/shader_reflection.cpp
  pipeline/pipeline.cpp
  pipeline/framebuffer.cpp
  pipeline/triangle_pass.cpp
  pipeline/cube_pass.cpp
  pipeline/execution_group.cpp
  pipeline/render_graph.cpp
  pipeline/acceleration_structure.cpp
  pipeline/raytracing_pipeline.cpp
  # loaders
  loaders/gltf_loader.cpp
  loaders/ply_loader.cpp
  loaders/miniply.cpp
  loaders/ibl.cpp
)

# Include paths:
#   BUILD:   ${CMAKE_SOURCE_DIR} so #include <vkwave/core/buffer.h> works
#   INSTALL: ${CMAKE_INSTALL_INCLUDEDIR} (standard install tree)
target_include_directories(${TARGET_NAME}
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "${CGLTF_INCLUDE_DIR}"
    "${STB_INCLUDE_DIR}"
)

target_link_libraries(${TARGET_NAME}
  PUBLIC build
  ${SPDLOG_TARGET}
  ${GLFW_TARGET}
  Vulkan::Vulkan
  spirv-reflect::spirv-reflect-static
  vkwave::shaderc
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
)

# Vulkan dynamic dispatch + GLM depth range
target_compile_definitions(${TARGET_NAME}
  PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE
)


# LTO for GCC/Clang only
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  include(spsCompilerPlatformFlags)
  sps_link_optimization(${TARGET_NAME})
endif()

# Export GLFW target for app
set(GLFW_TARGET ${GLFW_TARGET} PARENT_SCOPE)
