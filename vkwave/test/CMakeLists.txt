add_executable(core_test
  core_test.cpp
)

target_link_libraries(core_test
  PRIVATE
    vkwave
    Catch2::Catch2WithMain
)

sps_catch_discover_tests(core_test)

add_executable(pipeline_test
  pipeline_test.cpp
)

target_link_libraries(pipeline_test
  PRIVATE
    vkwave
    Catch2::Catch2WithMain
)

target_compile_definitions(pipeline_test PRIVATE
  TEST_SHADER_DIR="${CMAKE_SOURCE_DIR}/vkwave/shaders/"
)

sps_catch_discover_tests(pipeline_test)
