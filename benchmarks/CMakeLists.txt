add_executable(bench bench.cc)
target_include_directories(bench PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(bench pthread)
