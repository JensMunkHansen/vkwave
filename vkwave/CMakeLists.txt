set(TARGET_NAME vkwave)

# --- Resolve dependencies (prefer system, fall back to FetchContent) ---

# spdlog
if(TARGET spdlog::spdlog)
  set(SPDLOG_TARGET spdlog::spdlog)
  message(STATUS "spdlog: using system package")
elseif(TARGET spdlog)
  set(SPDLOG_TARGET spdlog)
  message(STATUS "spdlog: using system package (spdlog)")
else()
  message(STATUS "spdlog: using FetchContent")
  FetchContent_MakeAvailable(spdlog)
  set(SPDLOG_TARGET spdlog::spdlog)
endif()

# glfw
if(TARGET glfw)
  set(GLFW_TARGET glfw)
  message(STATUS "glfw: using system package")
elseif(TARGET glfw3::glfw3)
  set(GLFW_TARGET glfw3::glfw3)
  message(STATUS "glfw: using system package (glfw3::glfw3)")
else()
  message(STATUS "glfw: using FetchContent")
  FetchContent_MakeAvailable(glfw)
  set(GLFW_TARGET glfw)
endif()

# imgui
if(NOT imgui_FOUND)
  message(STATUS "imgui: using FetchContent")
  FetchContent_MakeAvailable(imgui)
  set(_IMGUI_FROM_FETCH ON)
else()
  message(STATUS "imgui: using system package")
  set(_IMGUI_FROM_FETCH OFF)
endif()

# cgltf (header-only glTF parser)
if(NOT cgltf_FOUND)
  message(STATUS "cgltf: using FetchContent")
  FetchContent_MakeAvailable(cgltf)
  set(CGLTF_INCLUDE_DIR "${cgltf_SOURCE_DIR}")
else()
  message(STATUS "cgltf: using system package")
endif()

# stb (stb_image for texture loading)
if(NOT stb_FOUND)
  message(STATUS "stb: using FetchContent")
  FetchContent_MakeAvailable(stb)
  set(STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
else()
  message(STATUS "stb: using system package")
endif()

# --- Build ImGui backend library ---
if(_IMGUI_FROM_FETCH)
  add_library(imgui_impl STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
  )
  target_include_directories(imgui_impl PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
  target_link_libraries(imgui_impl PUBLIC
    ${GLFW_TARGET}
    Vulkan::Vulkan
  )
else()
  add_library(imgui_impl INTERFACE)
  target_link_libraries(imgui_impl INTERFACE imgui::imgui)
endif()

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
  # pipeline
  pipeline/shaders.cpp
  pipeline/pipeline.cpp
  pipeline/framebuffer.cpp
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

# Export imgui_impl target for consumers
set(GLFW_TARGET ${GLFW_TARGET} PARENT_SCOPE)
