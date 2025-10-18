add_executable(main main.cc)
target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(main pthread)
