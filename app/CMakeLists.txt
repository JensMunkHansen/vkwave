add_executable(vkwave_app main.cpp app_config.cpp)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  sps_link_optimization(vkwave_app)
endif()

target_link_libraries(vkwave_app
  PUBLIC
    vkwave
    imgui_impl
    Vulkan::Headers
  PRIVATE
    ${GLFW_TARGET}
    toml11::toml11
)

# Copy config to each per-config output directory (Ninja Multi-Config)
foreach(CFG ${CMAKE_CONFIGURATION_TYPES})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/vkwave.toml
    ${CMAKE_BINARY_DIR}/${CFG}/${CMAKE_INSTALL_BINDIR}/vkwave.toml COPYONLY)
endforeach()
