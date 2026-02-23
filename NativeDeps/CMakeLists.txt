cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(Dependencies
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CMake")

include(ExternalProject)
include(spsSBOM)

# Control verbosity of ExternalProject builds
option(DEPS_VERBOSE "Enable verbose output for dependency builds" OFF)
if(NOT DEPS_VERBOSE)
  set(EP_LOG_LEVEL OFF)
else()
  set(EP_LOG_LEVEL ON)
endif()

# --- Catch2 ---
option(BUILD_CATCH2 "Build and deploy Catch2 test framework" ON)
sps_get_version(CATCH2_VERSION "3.5.2")

if(BUILD_CATCH2)
  message(STATUS "Catch2 v${CATCH2_VERSION} will be built")
  ExternalProject_Add(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v${CATCH2_VERSION}
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
      -DBUILD_TESTING=OFF
      -DCATCH_INSTALL_DOCS=OFF
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- spdlog ---
option(BUILD_SPDLOG "Build and deploy spdlog" ON)
sps_get_version(SPDLOG_VERSION "1.15.1")

if(BUILD_SPDLOG)
  message(STATUS "spdlog v${SPDLOG_VERSION} will be built")
  ExternalProject_Add(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v${SPDLOG_VERSION}
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
      -DSPDLOG_BUILD_EXAMPLE=OFF
      -DSPDLOG_BUILD_TESTS=OFF
      -DSPDLOG_BUILD_BENCH=OFF
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- GLFW ---
option(BUILD_GLFW "Build and deploy GLFW" ON)
sps_get_version(GLFW_VERSION "3.4")

if(BUILD_GLFW)
  message(STATUS "GLFW v${GLFW_VERSION} will be built")
  ExternalProject_Add(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        ${GLFW_VERSION}
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
      -DGLFW_BUILD_DOCS=OFF
      -DGLFW_BUILD_TESTS=OFF
      -DGLFW_BUILD_EXAMPLES=OFF
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- Dear ImGui ---
option(BUILD_IMGUI "Build and deploy Dear ImGui with GLFW/Vulkan backends" ON)
sps_get_version(IMGUI_VERSION "1.91.6")

if(BUILD_IMGUI)
  message(STATUS "Dear ImGui v${IMGUI_VERSION} will be built")

  # imgui has no CMakeLists.txt — we patch one in from NativeDeps/patches/
  set(_IMGUI_DEPENDS "")
  if(BUILD_GLFW)
    set(_IMGUI_DEPENDS DEPENDS glfw)
  endif()

  ExternalProject_Add(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v${IMGUI_VERSION}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -E copy
      ${CMAKE_CURRENT_SOURCE_DIR}/imgui_CMakeLists.txt
      <SOURCE_DIR>/CMakeLists.txt
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    ${_IMGUI_DEPENDS}
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- cgltf (header-only) ---
option(BUILD_CGLTF "Deploy cgltf header" ON)
sps_get_version(CGLTF_VERSION "1.14")

if(BUILD_CGLTF)
  message(STATUS "cgltf v${CGLTF_VERSION} will be deployed")
  ExternalProject_Add(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v${CGLTF_VERSION}
    GIT_SHALLOW    TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy
      <SOURCE_DIR>/cgltf.h ${CMAKE_INSTALL_PREFIX}/include/cgltf.h
    LOG_DOWNLOAD  ${EP_LOG_LEVEL}
  )
endif()

# --- toml11 (header-only) ---
option(BUILD_TOML11 "Build and deploy toml11" ON)
sps_get_version(TOML11_VERSION "4.4.0")

if(BUILD_TOML11)
  message(STATUS "toml11 v${TOML11_VERSION} will be built")
  ExternalProject_Add(toml11
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG        v${TOML11_VERSION}
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -Dtoml11_BUILD_TEST=OFF
      -Dtoml11_BUILD_EXAMPLES=OFF
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- SPIRV-Reflect ---
option(BUILD_SPIRV_REFLECT "Build and deploy SPIRV-Reflect" ON)
sps_get_version(SPIRV_REFLECT_VERSION "vulkan-sdk-1.4.304.1")

if(BUILD_SPIRV_REFLECT)
  message(STATUS "SPIRV-Reflect v${SPIRV_REFLECT_VERSION} will be built")
  ExternalProject_Add(spirv-reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG        ${SPIRV_REFLECT_VERSION}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -E copy
      ${CMAKE_CURRENT_SOURCE_DIR}/spirv_reflect_CMakeLists.txt
      <SOURCE_DIR>/CMakeLists.txt
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- args (header-only CLI parser) ---
option(BUILD_ARGS "Build and deploy args" ON)
sps_get_version(ARGS_VERSION "6.4.7")

if(BUILD_ARGS)
  message(STATUS "args v${ARGS_VERSION} will be built")
  ExternalProject_Add(args
    GIT_REPOSITORY https://github.com/Taywee/args.git
    GIT_TAG        ${ARGS_VERSION}
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DARGS_BUILD_EXAMPLE=OFF
      -DARGS_BUILD_UNITTESTS=OFF
    LOG_BUILD     ${EP_LOG_LEVEL}
    LOG_CONFIGURE ${EP_LOG_LEVEL}
    LOG_INSTALL   ${EP_LOG_LEVEL}
  )
endif()

# --- stb (header-only) ---
option(BUILD_STB "Deploy stb headers" ON)

if(BUILD_STB)
  message(STATUS "stb will be deployed")
  ExternalProject_Add(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND
      ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/include/stb
      COMMAND ${CMAKE_COMMAND} -E copy
        <SOURCE_DIR>/stb_image.h ${CMAKE_INSTALL_PREFIX}/include/stb/stb_image.h
      COMMAND ${CMAKE_COMMAND} -E copy
        <SOURCE_DIR>/stb_image_write.h ${CMAKE_INSTALL_PREFIX}/include/stb/stb_image_write.h
    LOG_DOWNLOAD  ${EP_LOG_LEVEL}
  )
endif()
