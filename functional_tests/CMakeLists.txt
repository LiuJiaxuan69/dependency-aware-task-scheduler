find_package(GTest REQUIRED)
find_package(spdlog REQUIRED)

add_executable(tests test.cc gtest_main.cc)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tests GTest::gtest pthread spdlog::spdlog)

enable_testing()
add_test(NAME all_test COMMAND tests)
