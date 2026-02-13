add_executable(coretest
  coretest.cpp
)

target_link_libraries(coretest
  PRIVATE
    vkwave
    Catch2::Catch2WithMain
)

sps_catch_discover_tests(coretest)
