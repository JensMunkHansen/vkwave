cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extra policies (not covering all introduced after 3.12)
foreach(policy
    CMP0083 # CMake 3.14
    CMP0127
    )
  if(POLICY ${policy})
    cmake_policy(SET ${policy} NEW)
  endif()
endforeach()

include(CMakeDependentOption)

# Download dependencies through CMake
include(dependencies.cmake)

project(vkwave
  VERSION 0.1
  DESCRIPTION "Async GPU rendering engine"
  LANGUAGES C CXX)

# Set up build directories (GNUInstallDirs layout, RPATH, multi-config output dirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")
include(spsInstallDirs)

# Create interface target for compiler/platform flags and include them
add_library(build INTERFACE)
include(spsCompilerPlatformFlags)

option(VKWAVE_FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)
option(VKWAVE_DEBUG "Enable debug logging and validation" OFF)

# Auto-enable debug for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
  set(VKWAVE_DEBUG ON)
endif()

if(VKWAVE_FORCE_COLORED_OUTPUT)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
  endif()
endif()

find_package(Vulkan REQUIRED)

add_subdirectory(vkwave)
add_subdirectory(app)
