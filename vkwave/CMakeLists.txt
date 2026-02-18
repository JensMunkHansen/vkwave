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

# --- Shader compilation ---
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
  message(FATAL_ERROR "glslc not found — install the Vulkan SDK")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

file(GLOB SHADER_SOURCES
  "${SHADER_SOURCE_DIR}/*.vert"
  "${SHADER_SOURCE_DIR}/*.frag"
  "${SHADER_SOURCE_DIR}/*.comp"
)

foreach(SHADER ${SHADER_SOURCES})
  get_filename_component(SHADER_NAME ${SHADER} NAME)
  set(SHADER_SPV "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
  add_custom_command(
    OUTPUT ${SHADER_SPV}
    COMMAND ${GLSLC} -o ${SHADER_SPV} ${SHADER}
    DEPENDS ${SHADER}
    COMMENT "Compiling shader ${SHADER_NAME}"
  )
  list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV_FILES})

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
  pipeline/pipeline.cpp
  pipeline/framebuffer.cpp
  pipeline/triangle_pass.cpp
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

add_dependencies(${TARGET_NAME} shaders)

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
  ${Vulkan_LIBRARY}
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
