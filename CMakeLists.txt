cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

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

project(vkwave
  VERSION 0.1
  DESCRIPTION "Async GPU rendering engine"
  LANGUAGES C CXX)

# Set up build directories (GNUInstallDirs layout, RPATH, multi-config output dirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")
include(spsInstallDirs)
include(spsTargets)

# Find dependencies (installed by build_dependencies.sh or system packages)
include(dependencies.cmake)

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

include(spsVulkan)

option(BUILD_TESTING "Build tests" ON)
include(spsTesting)

# LLVM source-based code coverage
option(SPS_LLVM_COVERAGE "Enable LLVM code coverage instrumentation (requires Clang)" OFF)
if(SPS_LLVM_COVERAGE)
  include(spsLLVMCoverage)
  sps_enable_llvm_coverage()
endif()

include(FetchAssets.cmake)

add_subdirectory(vkwave)
add_subdirectory(app)

if(BUILD_TESTING)
  add_subdirectory(vkwave/test)
endif()

# Combined coverage target: runs both test executables, merges profile data,
# reports on the shared library + tests + app.
if(SPS_LLVM_COVERAGE AND BUILD_TESTING)
  _sps_find_llvm_tools()
  if(LLVM_PROFDATA AND LLVM_COV)
    add_custom_target(coverage
      # Run test executables and app to generate profile data.
      # The app exits after VKWAVE_MAX_FRAMES (set to 10 for coverage builds).
      COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/core_test.profraw
              $<TARGET_FILE:core_test>
      COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/pipeline_test.profraw
              $<TARGET_FILE:pipeline_test>
      COMMAND ${CMAKE_COMMAND} -E chdir $<TARGET_FILE_DIR:vkwave_app>
              ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/vkwave_app.profraw
              $<TARGET_FILE:vkwave_app> --max-frames 10

      # Merge all profile data
      COMMAND ${LLVM_PROFDATA} merge -sparse
              ${CMAKE_BINARY_DIR}/core_test.profraw
              ${CMAKE_BINARY_DIR}/pipeline_test.profraw
              ${CMAKE_BINARY_DIR}/vkwave_app.profraw
              -o ${CMAKE_BINARY_DIR}/coverage.profdata

      # Generate text report (all objects: shared lib + tests + app)
      COMMAND ${LLVM_COV} report
              $<TARGET_FILE:core_test>
              -object=$<TARGET_FILE:pipeline_test>
              -object=$<TARGET_FILE:vkwave_app>
              -object=$<TARGET_FILE:vkwave>
              -instr-profile=${CMAKE_BINARY_DIR}/coverage.profdata
              -ignore-filename-regex=NativeDeps\|_deps\|test/\|vkwavepkg\|stb_image

      # Generate HTML report
      COMMAND ${LLVM_COV} show
              $<TARGET_FILE:core_test>
              -object=$<TARGET_FILE:pipeline_test>
              -object=$<TARGET_FILE:vkwave_app>
              -object=$<TARGET_FILE:vkwave>
              -instr-profile=${CMAKE_BINARY_DIR}/coverage.profdata
              -format=html
              -output-dir=${CMAKE_BINARY_DIR}/coverage-html
              -ignore-filename-regex=NativeDeps\|_deps\|test/\|vkwavepkg\|stb_image

      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Running tests + app and generating LLVM coverage report"
      DEPENDS core_test pipeline_test vkwave_app vkwave
      VERBATIM
    )
    message(STATUS "Combined coverage target 'coverage' created")
    message(STATUS "  HTML report: ${CMAKE_BINARY_DIR}/coverage-html/index.html")
  endif()
endif()
