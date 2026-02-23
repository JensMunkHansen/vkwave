# Find dependencies (installed by build_dependencies.sh or system packages)
#
# System-installed packages are preferred by default.
# Run ./build_dependencies.sh to install all dependencies to ~/vkwavepkg.

include(CMakeDependentOption)

# --- Options for using system-installed packages ---
option(VKWAVE_USE_SYSTEM_SPDLOG "Use system-installed spdlog"  ON)
option(VKWAVE_USE_SYSTEM_GLFW   "Use system-installed GLFW"    ON)
option(VKWAVE_USE_SYSTEM_IMGUI  "Use system-installed imgui"   ON)
option(VKWAVE_USE_SYSTEM_CGLTF  "Use system-installed cgltf"   ON)
option(VKWAVE_USE_SYSTEM_STB    "Use system-installed stb"     ON)

# --- spdlog ---
if(VKWAVE_USE_SYSTEM_SPDLOG)
  find_package(spdlog QUIET)
  if(spdlog_FOUND)
    message(STATUS "spdlog: found system package")
  else()
    message(FATAL_ERROR "spdlog not found. Run ./build_dependencies.sh first.")
  endif()
endif()

# --- GLFW ---
if(VKWAVE_USE_SYSTEM_GLFW)
  find_package(glfw3 QUIET)
  if(glfw3_FOUND)
    message(STATUS "glfw: found system package")
  else()
    message(FATAL_ERROR "GLFW not found. Run ./build_dependencies.sh first.")
  endif()
endif()

# --- Dear ImGui ---
if(VKWAVE_USE_SYSTEM_IMGUI)
  find_package(imgui CONFIG QUIET)
  if(imgui_FOUND)
    message(STATUS "imgui: found system package")
  else()
    message(FATAL_ERROR "imgui not found. Run ./build_dependencies.sh first.")
  endif()
endif()

# --- cgltf (header-only) ---
if(VKWAVE_USE_SYSTEM_CGLTF)
  find_path(CGLTF_INCLUDE_DIR cgltf.h)
  if(CGLTF_INCLUDE_DIR)
    set(cgltf_FOUND TRUE)
    message(STATUS "cgltf: found at ${CGLTF_INCLUDE_DIR}")
  else()
    message(FATAL_ERROR "cgltf not found. Run ./build_dependencies.sh first.")
  endif()
endif()

# --- toml11 ---
option(VKWAVE_USE_SYSTEM_TOML11 "Use system-installed toml11" ON)
if(VKWAVE_USE_SYSTEM_TOML11)
  find_package(toml11 CONFIG QUIET)
  if(toml11_FOUND)
    message(STATUS "toml11: found system package")
  else()
    message(FATAL_ERROR "toml11 not found. Run ./build_dependencies.sh first.")
  endif()
endif()

# --- SPIRV-Reflect ---
find_package(spirv-reflect-static CONFIG QUIET)
if(spirv-reflect-static_FOUND)
  message(STATUS "SPIRV-Reflect: found system package")
else()
  message(FATAL_ERROR "SPIRV-Reflect not found. Run ./build_dependencies.sh first.")
endif()

# --- args (CLI parser) ---
include(spsArgs)

# --- stb (header-only) ---
if(VKWAVE_USE_SYSTEM_STB)
  find_path(STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb)
  if(STB_INCLUDE_DIR)
    set(stb_FOUND TRUE)
    message(STATUS "stb: found at ${STB_INCLUDE_DIR}")
  else()
    message(FATAL_ERROR "stb not found. Run ./build_dependencies.sh first.")
  endif()
endif()
