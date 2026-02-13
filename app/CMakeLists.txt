add_executable(vkwave_app main.cpp)

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
)
