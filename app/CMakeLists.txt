add_executable(vkwave_app
  main.cpp
  engine.cpp
  scene.cpp
  scene_data.cpp
  scene_pipeline.cpp
  input.cpp
  app_config.cpp
  cli.cpp
)

# MSVC 14.44 ICE (C1001) during LTCG — skip on MSVC
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  sps_link_optimization(vkwave_app)
endif()
sps_copy_runtime_dlls(vkwave_app)

target_link_libraries(vkwave_app
  PUBLIC
    vkwave
    imgui_impl
    Vulkan::Headers
  PRIVATE
    ${GLFW_TARGET}
    toml11::toml11
    taywee::args
)

# Generate vkwave.toml from template, then copy to each per-config output directory.
# SPS_LLVM_COVERAGE builds get max_frames=10 so the app exits for automated coverage.
if(NOT DEFINED VKWAVE_MAX_FRAMES)
  if(SPS_LLVM_COVERAGE)
    set(VKWAVE_MAX_FRAMES 10)
  else()
    set(VKWAVE_MAX_FRAMES 0)
  endif()
endif()

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/vkwave.toml.in
  ${CMAKE_CURRENT_BINARY_DIR}/vkwave.toml
  @ONLY
)

foreach(CFG ${CMAKE_CONFIGURATION_TYPES})
  configure_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vkwave.toml
    ${CMAKE_BINARY_DIR}/${CFG}/${CMAKE_INSTALL_BINDIR}/vkwave.toml
    COPYONLY
  )
endforeach()
